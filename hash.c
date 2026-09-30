#include <stdint.h>
#include <string.h>

//NOTE: This is only intended for use in file identification
//Please do not use this in a cryptographic setting as no attempts have been
//made at avoiding side channel attacks

static uint32_t rotleft(uint32_t val, uint32_t shift)
{
	return val << shift | val >> (32-shift);
}

static void sha1_step(uint32_t *state, uint32_t f, uint32_t k, uint32_t w)
{
	uint32_t tmp = rotleft(state[0], 5) + f + state[4] + k + w;
	state[4] = state[3];
	state[3] = state[2];
	state[2] = rotleft(state[1], 30);
	state[1] = state[0];
	state[0] = tmp;
}

static void sha1_chunk(uint8_t *chunk, uint32_t *hash)
{
	uint32_t state[5], w[80];
	memcpy(state, hash, sizeof(state));
	for (uint32_t src = 0; src < 64; src += 4)
	{
		w[src >> 2] = chunk[src] << 24 | chunk[src+1] << 16 | chunk[src+2] << 8 | chunk[src+3];
	}
	for (uint32_t cur = 16; cur < 80; cur++)
	{
		w[cur] = rotleft(w[cur-3] ^ w[cur-8] ^ w[cur-14] ^ w[cur-16], 1);
	}
	for (uint32_t cur = 0; cur < 20; cur++)
	{
		sha1_step(state, (state[1] & state[2]) | ((~state[1]) & state[3]), 0x5A827999, w[cur]);
	}
	for (uint32_t cur = 20; cur < 40; cur++)
	{
		sha1_step(state, state[1] ^ state[2] ^ state[3], 0x6ED9EBA1, w[cur]);
	}
	for (uint32_t cur = 40; cur < 60; cur++)
	{
		sha1_step(state, (state[1] & state[2]) | (state[1] & state[3]) | (state[2] & state[3]), 0x8F1BBCDC, w[cur]);
	}
	for (uint32_t cur = 60; cur < 80; cur++)
	{
		sha1_step(state, state[1] ^ state[2] ^ state[3], 0xCA62C1D6, w[cur]);
	}
	for (uint32_t i = 0; i < 5; i++)
	{
		hash[i] += state[i];
	}
}

void sha1(uint8_t *data, uint64_t size, uint8_t *out)
{
	uint32_t hash[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
	uint8_t last[128];
	uint32_t last_size = 0;
	if ((size & 63) != 0) {
		for (uint32_t src = size - (size & 63); src < size; src++)
		{
			last[last_size++] = data[src];
		}
	}
	uint64_t bitsize = size * 8;
	size -= last_size;
	last[last_size++] = 0x80;
	while ((last_size & 63) != 56)
	{
		last[last_size++] = 0;
	}
	
	last[last_size++] = bitsize >> 56;
	last[last_size++] = bitsize >> 48;
	last[last_size++] = bitsize >> 40;
	last[last_size++] = bitsize >> 32;
	last[last_size++] = bitsize >> 24;
	last[last_size++] = bitsize >> 16;
	last[last_size++] = bitsize >> 8;
	last[last_size++] = bitsize;
	
	for (uint64_t cur = 0; cur < size; cur += 64)
	{
		sha1_chunk(data + cur, hash);
	}
	for (uint64_t cur = 0; cur < last_size; cur += 64)
	{
		sha1_chunk(last + cur, hash);
	}
	for (uint32_t cur = 0; cur < 20; cur += 4)
	{
		uint32_t val = hash[cur >> 2];
		out[cur] = val >> 24;
		out[cur+1] = val >> 16;
		out[cur+2] = val >> 8;
		out[cur+3] = val;
	}
}

static const uint32_t sha256_k[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static uint32_t rotr(uint32_t val, uint32_t shift)
{
	return val >> shift | val << (32 - shift);
}

static void sha256_chunk(uint8_t *chunk, uint32_t *hash)
{
	uint32_t w[64];
	for (int i = 0; i < 16; i++)
	{
		w[i] = chunk[i*4] << 24 | chunk[i*4+1] << 16 | chunk[i*4+2] << 8 | chunk[i*4+3];
	}
	for (int i = 16; i < 64; i++)
	{
		uint32_t s0 = rotr(w[i-15], 7) ^ rotr(w[i-15], 18) ^ (w[i-15] >> 3);
		uint32_t s1 = rotr(w[i-2], 17) ^ rotr(w[i-2], 19) ^ (w[i-2] >> 10);
		w[i] = w[i-16] + s0 + w[i-7] + s1;
	}
	uint32_t a = hash[0], b = hash[1], c = hash[2], d = hash[3], e = hash[4], f = hash[5], g = hash[6], h = hash[7];
	for (int i = 0; i < 64; i++)
	{
		uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
		uint32_t ch = (e & f) ^ (~e & g);
		uint32_t tmp1 = h + s1 + ch + sha256_k[i] + w[i];
		uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
		uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
		uint32_t tmp2 = s0 + maj;
		h = g;
		g = f;
		f = e;
		e = d + tmp1;
		d = c;
		c = b;
		b = a;
		a = tmp1 + tmp2;
	}
	hash[0] += a;
	hash[1] += b;
	hash[2] += c;
	hash[3] += d;
	hash[4] += e;
	hash[5] += f;
	hash[6] += g;
	hash[7] += h;
}

void sha256(uint8_t *data, uint64_t size, uint8_t *out)
{
	uint32_t hash[8] = {
		0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
	};
	uint64_t full = size & ~63ULL;
	for (uint64_t cur = 0; cur < full; cur += 64)
	{
		sha256_chunk(data + cur, hash);
	}
	//final block(s): remaining bytes, the 0x80 terminator and the bit length
	uint8_t last[128] = {0};
	uint32_t last_size = size - full;
	memcpy(last, data + full, last_size);
	last[last_size++] = 0x80;
	uint32_t total = last_size + 8 > 64 ? 128 : 64;
	uint64_t bitsize = size * 8;
	for (int i = 0; i < 8; i++)
	{
		last[total - 1 - i] = bitsize >> (i * 8);
	}
	for (uint32_t cur = 0; cur < total; cur += 64)
	{
		sha256_chunk(last + cur, hash);
	}
	for (int i = 0; i < 8; i++)
	{
		out[i*4] = hash[i] >> 24;
		out[i*4+1] = hash[i] >> 16;
		out[i*4+2] = hash[i] >> 8;
		out[i*4+3] = hash[i];
	}
}
