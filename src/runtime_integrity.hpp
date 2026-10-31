// SPDX-License-Identifier: MIT
#pragma once
#include <windows.h>
#include <bcrypt.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>
namespace app {
inline std::string fileHash(const std::filesystem::path &file){
    std::ifstream stream(file,std::ios::binary);if(!stream)throw std::runtime_error("A required file is missing");
    struct Hash {BCRYPT_ALG_HANDLE algorithm{};BCRYPT_HASH_HANDLE object{};~Hash(){if(object)BCryptDestroyHash(object);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);}} h;
    auto checked=[](NTSTATUS s){if(s<0)throw std::runtime_error("SHA-256 file verification failed");};
    checked(BCryptOpenAlgorithmProvider(&h.algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0));
    DWORD bytes{},used{};checked(BCryptGetProperty(h.algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&bytes),sizeof(bytes),&used,0));
    std::vector<UCHAR> storage(bytes);checked(BCryptCreateHash(h.algorithm,&h.object,storage.data(),bytes,nullptr,0,0));
    char buffer[65536];while(stream){stream.read(buffer,sizeof(buffer));if(stream.gcount())checked(BCryptHashData(h.object,reinterpret_cast<PUCHAR>(buffer),ULONG(stream.gcount()),0));}
    if(!stream.eof())throw std::runtime_error("A required file could not be read completely");
    UCHAR digest[32]{};checked(BCryptFinishHash(h.object,digest,32,0));
    std::ostringstream result;for(auto v:digest)result<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(v);return result.str();
}
inline void verifyRuntime(const std::filesystem::path &root,bool fg){
    for(auto entry:{std::pair{L"amd_fidelityfx_loader_vk.dll","38a56a01fb3565c0757d9f5c1e4e69de4b65e5f2e5769b323b200dc1f142bab9"},
                   {L"amd_fidelityfx_upscaler_vk.dll","4a7e895af6e0b615d40a84d59eb8c2b1054ad91cdd0c739567940270d51bdab7"},
                   {L"amd_fidelityfx_framegeneration_vk.dll","d002dabff8c5e71eaa715fc99438cd4dc81230262a7897b16bc402efec3498b4"}}){
        if(!fg&&!wcscmp(entry.first,L"amd_fidelityfx_framegeneration_vk.dll"))continue;
        if(fileHash(root/entry.first)!=entry.second)throw std::runtime_error("AMD runtime differs from the verified SDK 2.4 package");
    }
}
} // namespace app
