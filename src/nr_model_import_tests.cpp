// SPDX-License-Identifier: MIT
#include "nr_model_import.hpp"
#include <iostream>
static void check(bool value,const char *why){if(!value)throw std::runtime_error(why);}
int wmain(int argc,wchar_t **argv){try{
    check(argc==2||argc==3,"Fresh test folder required");std::filesystem::path root=argv[1];check(!std::filesystem::exists(root),"Fixture already exists");
    std::filesystem::create_directories(root/L"nr/dlssnr-amd");std::filesystem::create_directories(root/L"staging");
    const std::wstring job=L"0123456789abcdef0123456789abcdef";
    bool refused=false;try{xpfsr::nr::modelImportStatus(root,L"../outside");}catch(...){refused=true;}check(refused,"Import status escaped its job folder");
    xpfsr::nr::writeModelImportProgress(root,job,"extracting","FSR continues");auto progress=xpfsr::nr::readModelImportProgress(root,job);
    check(progress.phase=="extracting"&&progress.message=="FSR continues","Import progress did not round trip");
    auto status=xpfsr::nr::modelImportStatus(root,job);
    {std::ofstream out(status);out<<"XPFSR24_NR_IMPORT_V1 00000000000000000000000000000000\nsuccess\nstale result\n";}
    check(xpfsr::nr::readModelImportProgress(root,job).phase.empty(),"A stale job was reported successful");
    const auto existing=root/L"nr/dlssnr-amd/dlssnr.bin";{std::ofstream out(existing);out<<"original user data";}
    auto before=app::fileHash(existing);
    {std::ofstream out(root/L"staging/dlssnr-small.bin");out<<"untrusted model";}
    refused=false;try{xpfsr::nr::installExtractedModels(root,root/L"staging",false);}catch(...){refused=true;}
    check(refused&&app::fileHash(existing)==before&&!std::filesystem::exists(root/L"nr/dlssnr-amd/dlssnr-small.bin"),"Invalid model published or existing data changed");
    check(!std::filesystem::exists(root/L"nr/USER-MODELS.ini"),"Failed import recorded model verification");
    check(!xpfsr::nr::acceptedModel(true,113917248,"untrusted")&&!xpfsr::nr::acceptedModel(false,147756560,"untrusted"),"Model hash gate accepted invalid data");
    if(argc==3){
        const std::filesystem::path verified=argv[2], positive=root/L"positive", staging=positive/L"staging";
        std::filesystem::create_directories(staging);
        for(auto name:{L"dlssnr-small.bin",L"dlssnr.bin"})std::filesystem::copy_file(verified/name,staging/name);
        // A corrupt second output must not publish the valid first output.
        {std::ofstream out(staging/L"dlssnr.bin",std::ios::trunc);out<<"corrupt second model";}
        refused=false;try{xpfsr::nr::installExtractedModels(positive,staging,false);}catch(...){refused=true;}
        check(refused&&!std::filesystem::exists(positive/L"nr/dlssnr-amd/dlssnr-small.bin"),"Partially verified model set was installed");
        std::filesystem::copy_file(verified/L"dlssnr.bin",staging/L"dlssnr.bin",std::filesystem::copy_options::overwrite_existing);
        check(xpfsr::nr::installExtractedModels(positive,staging,false)==2,"First import did not install both verified models");
        const auto small=positive/L"nr/dlssnr-amd/dlssnr-small.bin", original=positive/L"nr/dlssnr-amd/dlssnr.bin";
        check(app::fileHash(small)==xpfsr::nr::smallModelSha&&app::fileHash(original)==xpfsr::nr::originalModelSha,"Installed model hashes differ");
        auto modified=std::filesystem::last_write_time(small);
        for(auto name:{L"dlssnr-small.bin",L"dlssnr.bin"})std::filesystem::copy_file(verified/name,staging/name);
        check(xpfsr::nr::installExtractedModels(positive,staging,false)==2&&std::filesystem::last_write_time(small)==modified,"Identical reimport replaced a live model");
        const auto conflict=root/L"conflict", conflictStage=conflict/L"staging";
        std::filesystem::create_directories(conflictStage);std::filesystem::create_directories(conflict/L"nr/dlssnr-amd");
        for(auto name:{L"dlssnr-small.bin",L"dlssnr.bin"})std::filesystem::copy_file(verified/name,conflictStage/name);
        const auto conflictTarget=conflict/L"nr/dlssnr-amd/dlssnr.bin";
        {std::ofstream out(conflictTarget);out<<"existing local model";}
        auto old=app::fileHash(conflictTarget);refused=false;
        try{xpfsr::nr::installExtractedModels(conflict,conflictStage,false);}catch(...){refused=true;}
        check(refused&&app::fileHash(conflictTarget)==old&&!std::filesystem::exists(conflict/L"nr/dlssnr-amd/dlssnr-small.bin"),"In-sim import overwrote existing data");
        check(xpfsr::nr::installExtractedModels(conflict,conflictStage,true)==2,"Offline replacement failed");
        auto backup=conflictTarget;backup+=L".previous-"+std::wstring(old.begin(),old.end())+L".bin";
        check(app::fileHash(backup)==old,"Offline replacement lost the previous model");
        std::cout<<"PASS: real extracted models, complete-set validation, identical live reimport, protected existing models and verified offline backup.\n";
    }
    std::cout<<"PASS: job/token isolation, bounded progress, invalid model rejection and existing-data preservation.\n";return 0;
}catch(const std::exception &e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
