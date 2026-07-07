#include "likegit/sha1.hpp"
#include <vector>
#include <cstdint>
#include <sstream>
#include <iomanip>

uint32_t rotate_left(uint32_t value, int shift) {
    return (value << shift) | (value >> (32 - shift));
}

std::string manual_sha1(const std::string& input) {
    uint32_t h0 = 0x67452301;
    uint32_t h1 = 0xEFCDAB89;
    uint32_t h2 = 0x98BADCFE;
    uint32_t h3 = 0x10325476;
    uint32_t h4 = 0xC3D2E1F0;
    
    uint64_t L = input.length() * 8;
    std::vector<uint8_t> padded(input.begin(), input.end());
    padded.push_back(0x80);
    
    while (((padded.size() * 8) % 512) != 448){
        padded.push_back(0x00);
    }

    for (int i = 0; i < 8; i++){
        padded.push_back((L >> ((7-i)*8)) & 0xFF);
    }

    for(int i = 0; i < padded.size();) {
        std::vector<uint32_t> w(80);
        for (int j = 0; j < 16; j++){
            w[j] = padded[i + j*4] << 24 | padded[i + j*4 + 1] << 16 | padded[i + j*4 + 2] << 8 | padded[i + j*4 + 3];
        }
        for (int j = 16; j < 80; j++) {
            w[j] = rotate_left(w[j-3] ^ w[j-8] ^ w[j-14] ^ w[j-16], 1);
        }
        uint32_t a = h0;
        uint32_t b = h1;
        uint32_t c = h2;
        uint32_t d = h3;
        uint32_t e = h4;

        uint32_t f,k;
        
        for (int j = 0; j<80; j++) {
            if(j<20){
                f = (b & c) | (~b & d);
                k = 0x5A827999;
            } else if (j<40){
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (j<60){
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            uint32_t temp = rotate_left(a, 5) + f + e + k + w[j];
            e = d;
            d = c;
            c = rotate_left(b, 30);
            b = a;
            a = temp;
        }

        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;

        i += 64;
    }
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    ss << std::setw(8) << h0
       << std::setw(8) << h1
       << std::setw(8) << h2
       << std::setw(8) << h3
       << std::setw(8) << h4;
    return ss.str();
}

