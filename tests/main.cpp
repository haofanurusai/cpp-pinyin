#include <filesystem>
#include <cpp-pinyin/config.h>

#include <cpp-pinyin/Pinyin.h>
#include <cpp-pinyin/G2pglobal.h>
#include <iostream>

#include "JyuptingTest.h"
#include "ManTest.h"

using namespace Test;

int main(int argc, char *argv[]) {
#if CPP_PINYIN_ENABLE_FILE_IO
    const auto applicationDirPath = std::filesystem::current_path() / "dict";
    Pinyin::setDictionaryPath(applicationDirPath);
    std::cout << "Path: " << applicationDirPath << std::endl;
#endif

    const ManTest manTest;
    std::cout << "Pinyin G2P test: " << std::endl;
    std::cout << "--------------------" << std::endl;
    std::cout << "apiTest: " << manTest.apiTest() << std::endl;
    std::cout << "--------------------" << std::endl;
    manTest.toneBatchTest();
    std::cout << "--------------------" << std::endl;
    manTest.unToneBatchTest();
    std::cout << "--------------------\n" << std::endl;

#if CPP_PINYIN_ENABLE_FILE_IO
    const JyuptingTest jyuptingTest;
    std::cout << "Cantonese G2P test:" << std::endl;
    std::cout << "--------------------" << std::endl;
    jyuptingTest.toneBatchTest();
    std::cout << "--------------------\n" << std::endl;
    jyuptingTest.untoneBatchTest();
    std::cout << "--------------------\n" << std::endl;
#endif

    std::cout << "G2P mix test:" << std::endl;
    std::cout << "--------------------" << std::endl;
    const auto g2p_man = std::make_unique<Pinyin::Pinyin>();

    const std::string raw2 =
        "举杯あャ坐ュ饮放あ歌竹林间/清风拂 面悄word然xax asx a xxs拨？Q！动初弦/便推开烦恼与尘喧/便还是当时的少年";
    const auto res1 = g2p_man->hanziToPinyin(raw2, Pinyin::ManTone::Style::TONE2, Pinyin::Error::Default, false,
                                             true).
                               toStdStr();

    std::cout << res1 << std::endl;

    std::cout << "--------------------" << std::endl;
    const auto res2 =
        g2p_man->hanziToPinyin(raw2, Pinyin::ManTone::Style::TONE3, Pinyin::Error::Ignore, false, true).
                 toStdStr();

    std::cout << res2 << std::endl;
    return 0;
}
