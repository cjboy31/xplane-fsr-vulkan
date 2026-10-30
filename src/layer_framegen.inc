// SPDX-License-Identifier: MIT
// Included after the device hooks. App-facing handles are distinct from SDK
// proxy handles so oldSwapchain destruction cannot destroy a recreated proxy.
static VKAPI_ATTR VkResult VKAPI_CALL L_DeviceWaitIdle(VkDevice device) {
    auto d = deviceFor(device);
    if (!sdkNesting) {
        std::shared_ptr<SwapchainGeneration> generation;
        {
            std::lock_guard<std::recursive_mutex> lock(d->lock);
            generation = d->fgCurrent;
        }
        if (generation) {
            try {
                SdkScope sdk;
                generation->presentation->wait();
            } catch (const std::exception &e) {
                log("FG idle wait failed: %s", e.what());
                return VK_ERROR_DEVICE_LOST;
            }
        }
    }
    return d->fn<PFN_vkDeviceWaitIdle>("vkDeviceWaitIdle")(device);
}
static VKAPI_ATTR VkResult VKAPI_CALL L_QueueWaitIdle(VkQueue queue) {
    auto d = deviceFor(queue);
    if (!sdkNesting && queue == d->fgQueues[0]) {
        std::shared_ptr<SwapchainGeneration> generation;
        {
            std::lock_guard<std::recursive_mutex> lock(d->lock);
            generation = d->fgCurrent;
        }
        if (generation) {
            try {
                SdkScope sdk;
                generation->presentation->wait();
            } catch (const std::exception &e) {
                log("FG queue wait failed: %s", e.what());
                return VK_ERROR_DEVICE_LOST;
            }
        }
    }
    return d->fn<PFN_vkQueueWaitIdle>("vkQueueWaitIdle")(queue);
}
static VKAPI_ATTR VkResult VKAPI_CALL L_CreateSwapchainKHR(VkDevice device,
                                                           const VkSwapchainCreateInfoKHR *info,
                                                           const VkAllocationCallbacks *allocator,
                                                           VkSwapchainKHR *output) {
    auto d = deviceFor(device);
    auto native = d->fn<PFN_vkCreateSwapchainKHR>("vkCreateSwapchainKHR");
    if (sdkNesting)
        return native(device, info, allocator, output);
    std::lock_guard<std::recursive_mutex> lock(d->lock);
    log("Swapchain request: %ux%u format=%u images=%u mode=%u pNext=%d usage=%x FG=%s",
        info->imageExtent.width, info->imageExtent.height, info->imageFormat, info->minImageCount,
        info->presentMode,
        info->pNext ? int(static_cast<const VkBaseInStructure *>(info->pNext)->sType) : 0,
        info->imageUsage, d->fgRequested.c_str());
    auto previous = d->fgSwapchains.find(info->oldSwapchain);
    try {
        if (previous != d->fgSwapchains.end()) {
            SdkScope sdk;
            stopGenerator(*d);
            auto result = previous->second->presentation->recreate(*info);
            if (result != VK_SUCCESS)
                return result;
            previous->second->retired = true;
            auto generation = std::make_shared<SwapchainGeneration>();
            generation->presentation = previous->second->presentation;
            generation->width = info->imageExtent.width;
            generation->height = info->imageExtent.height;
            generation->format = info->imageFormat;
            *output = reinterpret_cast<VkSwapchainKHR>(generation.get());
            d->fgSwapchains[*output] = generation;
            d->fgCurrent = generation;
            return VK_SUCCESS;
        }
        if (!d->fgRequested.empty() && !d->fgFailed && d->mode != Mode::Observe && !d->fgCurrent &&
            !allocator && !info->oldSwapchain && info->imageArrayLayers == 1 &&
            info->imageSharingMode == VK_SHARING_MODE_EXCLUSIVE && info->minImageCount <= 6) {
            VkBool32 supported = VK_FALSE;
            auto surface = d->inst<PFN_vkGetPhysicalDeviceSurfaceSupportKHR>(
                "vkGetPhysicalDeviceSurfaceSupportKHR");
            check(surface(d->physical, d->graphicsFamily, info->surface, &supported),
                  "FG present support");
            if (!supported)
                throw std::runtime_error("Reserved graphics queues cannot present to this surface");
            SdkScope sdk;
            ensureRuntime(*d);
            auto presentation = std::make_shared<xpfsr::Presentation>(d->runtime->api);
            presentation->create({d->handle, applicationPhysical(*d), d->next}, *info, d->fgQueues,
                                 d->graphicsFamily,d->fgPresenter);
            auto generation = std::make_shared<SwapchainGeneration>();
            generation->presentation = std::move(presentation);
            generation->width = info->imageExtent.width;
            generation->height = info->imageExtent.height;
            generation->format = info->imageFormat;
            *output = reinterpret_cast<VkSwapchainKHR>(generation.get());
            d->fgSwapchains[*output] = generation;
            d->fgCurrent = generation;
            strncpy(d->status.fgReason, "FG swapchain ready; waiting for scene depth and motion",
                    159);
            log("FG SDK presentation proxy ready: provider=%s requested=%s synchronized=%u colorspace=%u composite-alpha=%u",
                generation->presentation->provider().c_str(),d->fgPresenter.empty()?"automatic":d->fgPresenter.c_str(),d->fgSynchronized,
                info->imageColorSpace,info->compositeAlpha);
            return VK_SUCCESS;
        }
    } catch (const std::exception &e) {
        d->fgFailed = true;
        strncpy(d->status.fgReason, e.what(), 159);
        log("FG swapchain unavailable: %s", e.what());
        if (previous != d->fgSwapchains.end())
            return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!d->fgRequested.empty() && !d->fgCurrent) {
        strncpy(d->status.fgReason, "FG swapchain requirements unmet; native presentation", 159);
    }
    auto result = native(device, info, allocator, output);
    if (result == VK_SUCCESS)
        d->nativeSwapchains.insert(*output);
    return result;
}
static VKAPI_ATTR void VKAPI_CALL L_DestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain,
                                                        const VkAllocationCallbacks *allocator) {
    auto d = deviceFor(device);
    if (sdkNesting) {
        d->fn<PFN_vkDestroySwapchainKHR>("vkDestroySwapchainKHR")(device, swapchain, allocator);
        return;
    }
    std::shared_ptr<SwapchainGeneration> generation;
    {
        std::lock_guard<std::recursive_mutex> lock(d->lock);
        auto it = d->fgSwapchains.find(swapchain);
        if (it != d->fgSwapchains.end()) {
            generation = it->second;
            if (d->fgCurrent == generation) {
                stopGenerator(*d);
                d->fgCurrent.reset();
            }
            d->fgSwapchains.erase(it);
        } else
            d->nativeSwapchains.erase(swapchain);
    }
    if (generation) {
        SdkScope sdk;
        generation.reset();
    } else
        d->fn<PFN_vkDestroySwapchainKHR>("vkDestroySwapchainKHR")(device, swapchain, allocator);
}
static VKAPI_ATTR VkResult VKAPI_CALL L_GetSwapchainImagesKHR(VkDevice device,
                                                              VkSwapchainKHR swapchain,
                                                              uint32_t *count, VkImage *images) {
    auto d = deviceFor(device);
    std::shared_ptr<SwapchainGeneration> generation;
    {
        std::lock_guard<std::recursive_mutex> lock(d->lock);
        auto it = d->fgSwapchains.find(swapchain);
        if (it != d->fgSwapchains.end())
            generation = it->second;
    }
    if (generation) {
        SdkScope sdk;
        auto result = generation->presentation->functions.pOutGetSwapchainImagesKHR(
            device, generation->presentation->handle(), count, images);
        if (images && (result == VK_SUCCESS || result == VK_INCOMPLETE)) {
            std::lock_guard<std::recursive_mutex> lock(d->lock);
            for (uint32_t i = 0; i < *count; ++i)
                d->fgBackbuffers[images[i]] = generation;
        }
        return result;
    }
    return d->fn<PFN_vkGetSwapchainImagesKHR>("vkGetSwapchainImagesKHR")(device, swapchain, count,
                                                                         images);
}
static VKAPI_ATTR VkResult VKAPI_CALL L_AcquireNextImageKHR(VkDevice device,
                                                            VkSwapchainKHR swapchain,
                                                            uint64_t timeout, VkSemaphore semaphore,
                                                            VkFence fence, uint32_t *index) {
    auto d = deviceFor(device);
    std::shared_ptr<SwapchainGeneration> generation;
    {
        std::lock_guard<std::recursive_mutex> lock(d->lock);
        auto it = d->fgSwapchains.find(swapchain);
        if (it != d->fgSwapchains.end())
            generation = it->second;
    }
    if (generation) {
        SdkScope sdk;
        static unsigned traces = 0;
        if (++traces <= 2)
            for (auto q : d->fgQueues)
                log("SDK acquire queue=%p dispatch=%p", reinterpret_cast<void *>(q),
                    q ? *reinterpret_cast<void **>(q) : nullptr);
        return generation->presentation->functions.pOutAcquireNextImageKHR(
            device, generation->presentation->handle(), timeout, semaphore, fence, index);
    }
    return d->fn<PFN_vkAcquireNextImageKHR>("vkAcquireNextImageKHR")(device, swapchain, timeout,
                                                                     semaphore, fence, index);
}
static VKAPI_ATTR VkResult VKAPI_CALL L_AcquireNextImage2KHR(VkDevice device,
                                                             const VkAcquireNextImageInfoKHR *info,
                                                             uint32_t *index) {
    auto d = deviceFor(device);
    {
        std::lock_guard<std::recursive_mutex> lock(d->lock);
        if (d->fgSwapchains.count(info->swapchain)) {
            if (info->deviceMask != 1 || info->pNext)
                return VK_ERROR_FEATURE_NOT_PRESENT;
        }
    }
    if (d->fgSwapchains.count(info->swapchain))
        return L_AcquireNextImageKHR(device, info->swapchain, info->timeout, info->semaphore,
                                     info->fence, index);
    auto native = d->fn<PFN_vkAcquireNextImage2KHR>("vkAcquireNextImage2KHR");
    return native ? native(device, info, index) : VK_ERROR_FEATURE_NOT_PRESENT;
}
static VKAPI_ATTR void VKAPI_CALL L_SetHdrMetadataEXT(VkDevice device, uint32_t count,
                                                      const VkSwapchainKHR *chains,
                                                      const VkHdrMetadataEXT *metadata) {
    auto d = deviceFor(device);
    for (uint32_t i = 0; i < count; ++i) {
        std::shared_ptr<SwapchainGeneration> generation;
        {
            std::lock_guard<std::recursive_mutex> lock(d->lock);
            auto it = d->fgSwapchains.find(chains[i]);
            if (it != d->fgSwapchains.end())
                generation = it->second;
        }
        if (generation) {
            SdkScope sdk;
            auto handle = generation->presentation->handle();
            auto fn = generation->presentation->functions.pOutSetHdrMetadataEXT;
            if (fn)
                fn(device, 1, &handle, &metadata[i]);
        } else {
            auto native = d->fn<PFN_vkSetHdrMetadataEXT>("vkSetHdrMetadataEXT");
            if (native)
                native(device, 1, &chains[i], &metadata[i]);
        }
    }
}
static VkResult presentFrame(Device &d, VkQueue queue, const VkPresentInfoKHR *info) {
    auto native = d.fn<PFN_vkQueuePresentKHR>("vkQueuePresentKHR");
    // SDK presenter-thread traffic is already paced and uses its own native chain.
    if (sdkNesting || (!d.fgRequested.empty() && d.fgQueues[0] && queue != d.fgQueues[0]))
        return native(queue, info);
    // AMD requires Present (including its generation callback), PrepareV2 and
    // context destruction to be externally serialized. Keep the device guard
    // across the SDK call, not just the bookkeeping before/after it.
    std::lock_guard<std::recursive_mutex> presentationGuard(d.lock);
    LARGE_INTEGER presentStart{}, presentEnd{}, clockFrequency{};
    QueryPerformanceFrequency(&clockFrequency);
    QueryPerformanceCounter(&presentStart);
    std::shared_ptr<SwapchainGeneration> generation;
    {
        std::lock_guard<std::recursive_mutex> lock(d.lock);
        if (info->swapchainCount == 1) {
            auto it = d.fgSwapchains.find(info->pSwapchains[0]);
            if (it != d.fgSwapchains.end())
                generation = it->second;
        }
        if (!d.fgRequested.empty() && !generation && info->swapchainCount == 1 &&
            !d.nativeSwapchains.count(info->pSwapchains[0]))
            return native(queue, info);
        ++d.epoch;
        ++d.status.presents;
        d.attach();
        d.readControl();
        if (d.shared)
            d.enabled = InterlockedCompareExchange(&d.shared->enabled, 0, 0) != 0;
        if (generation) {
            if (info->pNext) {
                log("Unsupported FG present extension type %d",
                    int(static_cast<const VkBaseInStructure *>(info->pNext)->sType));
                return VK_ERROR_FEATURE_NOT_PRESENT;
            }
            bool active = d.enabled && !d.groundSafety.active && d.fgLiveEnabled && !d.fgFailed &&
                          d.frameGenerator && d.fgSequence.ready();
            if (d.frameGenerator) {
                SdkScope sdk;
                d.frameGenerator->configure(generation->presentation->handle(),
                                            d.fgSequence.prepared, active);
            }
            d.status.fgActive = active;
            if (!active)
                strncpy(d.status.fgReason,
                        d.groundSafety.active
                            ? "FG paused by ground camera safety; selection retained"
                        : d.enabled ? "FG bypass: submitted scene and prepared source must match"
                                    : "FG disabled with reconstruction toggle",
                        159);
            if (!active && d.frameGenerator &&
                (++d.fgSourceBypasses <= 3 || d.fgSourceBypasses % 300 == 0))
                log("FG source guard: prepared=%llu submitted=%llu scene=%llu consumed=%llu; "
                    "native present",
                    (unsigned long long)d.fgSequence.prepared,
                    (unsigned long long)d.fgSequence.submitted,
                    (unsigned long long)d.fgSequence.latestScene,
                    (unsigned long long)d.fgSequence.presented);
        }
        app::Camera camera{};
        if (app::readCamera(d.shared, camera)) {
            d.camera = camera;
            if (camera.frame == d.lastCameraFrame)
                d.camera.reset = 1;
            d.lastCameraFrame = camera.frame;
        } else
            d.camera.valid = 0;
    }
    VkResult result;
    if (generation) {
        SdkScope sdk;
        auto proxy = generation->presentation->handle();
        auto present = *info;
        present.pSwapchains = &proxy;
        result = generation->presentation->functions.pOutQueuePresentKHR(queue, &present);
        if(d.fgSynchronized && d.status.fgActive && (result==VK_SUCCESS||result==VK_SUBOPTIMAL_KHR)){
            // The compatibility path deliberately gives up CPU/GPU overlap.
            // Drain SDK-managed interpolation/composition/presents before the
            // next source can prepare on another host recording thread. Keep
            // the same device guard; SDK native present traffic bypasses it.
            // No semaphore is re-used and no old swapchain image is re-presented.
            try {generation->presentation->wait();}
            catch(const std::exception &e){
                d.fgFailed=true;d.status.fgActive=0;
                snprintf(d.status.fgReason,160,"FG synchronization failed: %.120s",e.what());
                log("FG compatibility synchronization failed: %s",e.what());
                return VK_ERROR_DEVICE_LOST;
            }
        }
    } else
        result = native(queue, info);
    QueryPerformanceCounter(&presentEnd);
    {
        std::lock_guard<std::recursive_mutex> lock(d.lock);
        if (clockFrequency.QuadPart > 0) {
            double scale = 1000.0 / clockFrequency.QuadPart;
            d.pacing.add(presentStart.QuadPart * scale, presentEnd.QuadPart * scale,
                         d.status.recordings != d.lastPresentRecordings);
            d.lastPresentRecordings = d.status.recordings;
            auto now = GetTickCount64();
            if (now - d.lastPacingReport >= 2000) {
                if(d.frameGenerator && d.frameGenerator->pixelProbe()){
                    auto *probe=d.frameGenerator->pixelProbe();probe->poll();auto &p=probe->stats;
                    log("FG pixels before composition: checked=%llu generated-blank-with-visible-source=%llu both-dark=%llu busy=%llu unsupported=%llu errors=%llu last-blank-frame=%llu source-mean=%.5f generated-mean=%.5f resets=%llu",
                        (unsigned long long)p.checked,(unsigned long long)p.generatedBlank,(unsigned long long)p.bothDark,
                        (unsigned long long)p.busy,(unsigned long long)p.unsupported,(unsigned long long)p.errors,
                        (unsigned long long)p.lastBlankFrame,p.realMean,p.generatedMean,(unsigned long long)d.frameGenerator->resetDispatches.load());
                }
                d.lastPacingReport = now;
                auto s = d.pacing.summary();
                log("Source pacing (CPU present-call intervals, not scanout): n=%u p50=%.2fms "
                    "p95=%.2fms p99=%.2fms max=%.2fms over50ms=%u present-block-p95=%.2fms "
                    "no-scene-presents-total=%u FG-active=%u",
                    s.samples, s.p50, s.p95, s.p99, s.maximum, s.over50ms, s.presentP95,
                    s.missingScene, d.status.fgActive);
                log("Renderer heartbeat: device=%p presents=%llu FSR-recordings=%llu bypasses=%llu enabled=%u last-pass-active=%u reason=%s",
                    reinterpret_cast<void*>(d.handle),(unsigned long long)d.status.presents,
                    (unsigned long long)d.status.recordings,(unsigned long long)d.status.bypasses,
                    d.enabled,d.status.active,d.status.reason);
            }
        }
        if (generation) {
            d.fgSequence.finishPresent();
            d.status.displayedPresents = generation->presentation->displayed();
            if (d.frameGenerator) {
                d.status.fgGenerated = d.frameGenerator->generated.load();
                auto failure = d.frameGenerator->failure.load();
                if (failure) {
                    d.fgFailed = true;
                    d.status.fgActive = 0;
                    snprintf(d.status.fgReason, 160,
                             "FG dispatch failed with SDK code %u; disable FG and restart",
                             failure);
                } else if (d.status.fgActive)
                    snprintf(d.status.fgReason,160,"%s; real-frame holds=%llu (missing source/error)",
                        d.fgSynchronized?"Synchronized compatibility FG":"SDK paced FG",
                        (unsigned long long)d.frameGenerator->skipped.load());
            }
        }
        if (d.status.presents < 5 || d.status.presents % 500 == 0)
            log("Presentation counters: rendered=%llu FG-prepared=%llu generated=%llu "
                "SDK-presents=%llu FG-active=%u",
                (unsigned long long)d.status.presents, (unsigned long long)d.status.fgPrepared,
                (unsigned long long)d.status.fgGenerated,
                (unsigned long long)d.status.displayedPresents, d.status.fgActive);
        if (!d.enabled) {
            d.status.active = 0;
            d.publish("Disabled; native simulator upscaling active");
        } else if (!d.camera.valid)
            d.publish("Waiting for camera plugin and an active flight");
        else
            d.publish(d.status.reason[0] ? d.status.reason : "Native presentation");
    }
    return result;
}
