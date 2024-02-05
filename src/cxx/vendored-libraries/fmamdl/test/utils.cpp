#pragma once

#include <cstdint>
#include <cassert>
#include <vector>
#include <span>
#include <string>
#include <string_view>



static_assert(sizeof(uint8_t) == sizeof(std::byte));



namespace utils {

	uint8_t base64ToBits(char c) {
		if(c >= 'A' && c <= 'Z') return  0 + (c - 'A');
		if(c >= 'a' && c <= 'z') return 26 + (c - 'a');
		if(c >= '0' && c <= '9') return 52 + (c - '0');
		if(c == '+') return 62;
		if(c == '/') return 63;
		return 0;
	}


	char bitsToBase64(uint8_t n) {
		if(n < 26) return (n -  0) + 'A';
		if(n < 52) return (n - 26) + 'a';
		if(n < 62) return (n - 52) + '0';
		if(n == 62) return '+';
		if(n == 63) [[likely]] return '/';
		return '=';
	}


	std::vector<std::byte> fromBase64(std::string_view str) {
		std::vector<std::byte> r;
		r.reserve(((str.size() * 3) / 4) + 1);

		size_t bitOffset = 0;

		const auto pushBits = [&](char c) {
			uint8_t bits     = base64ToBits(c);
			uint8_t bitShift = bitOffset % 8;
			auto&   back     = r.back();

			switch(bitShift) {
				case 0:
					r.push_back(std::byte(bits << 2));
					break;
				case 6:
					back = std::byte(uint8_t(back) | (bits >> 4));
					r.push_back(std::byte((bits & 0b001111) << 4));
					break;
				case 4:
					back = std::byte(uint8_t(back) | (bits >> 2));
					r.push_back(std::byte((bits & 0b000011) << 6));
					break;
				case 2:
					back = std::byte(uint8_t(back) | (bits >> 0));
					break;
			}

			bitOffset += 6;
		};

		for(char c : str) {
			if(c == '=') [[unlikely]] break;
			pushBits(c);
		}

		return r;
	}


	std::string toBase64(const std::span<const std::byte> data) {
		std::string r;
		r.reserve(((data.size() * 4) / 3) + 1);

		size_t  bitOffset     = 0;
		uint8_t remainingBits = 0;

		const auto pushBits = [&](uint8_t n) {
			uint8_t bitShift = bitOffset % 24;

			switch(bitShift) {
				case 0:
					r.push_back(bitsToBase64(n >> 2));
					remainingBits = n & 0b000011;
					break;
				case 8:
					r.push_back(bitsToBase64((remainingBits << 4) | (n >> 4)));
					remainingBits = n & 0b001111;
					break;
				case 16:
					r.push_back(bitsToBase64((remainingBits << 2) | (n >> 6)));
					r.push_back(bitsToBase64(n & 0b111111));
					remainingBits = 0;
					break;
			}

			bitOffset += 8;
		};

		for(std::byte n : data) {
			pushBits(uint8_t(n));
		}

		switch(bitOffset % 24) {
			case 0:  break;
			case 8:  r.push_back(bitsToBase64(remainingBits << 4)); break;
			case 16: r.push_back(bitsToBase64(remainingBits << 2)); break;
		}

		return r;
	}

}
