#include "sha256.hpp"
#include <iostream>
#include <stdexcept>

int main() {
    try {
        const auto check = [](std::string_view input, std::string_view expected) {
            atlas::detail::Sha256 whole;
            whole.update(input);
            if (whole.hexDigest() != expected)
                throw std::runtime_error("SHA-256 known-answer mismatch");
            atlas::detail::Sha256 chunks;
            for (std::size_t i = 0; i < input.size(); i += 17) chunks.update(input.substr(i, 17));
            if (chunks.hexDigest() != expected) throw std::runtime_error("SHA-256 partition mismatch");
        };
        check("", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
        check("abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        check("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
        check(std::string(1000000, 'a'), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
        atlas::detail::Sha256 resumed;
        resumed.update("a");
        (void)resumed.hexDigest();
        resumed.update("bc");
        if (resumed.hexDigest() != "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")
            throw std::runtime_error("digest changed incremental state");
        std::cout << "SHA-256 known answers, chunking and nonmutating digest passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
