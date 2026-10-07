#include "Hash.hpp"

#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

namespace
{

// Compact public-domain SHA-256 (based on Brad Conte's implementation).
struct Sha256Ctx
{
    std::uint8_t data[64]{};
    std::uint32_t datalen = 0;
    std::uint64_t bitlen = 0;
    std::uint32_t state[8]{0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu,
        0x1f83d9abu, 0x5be0cd19u};
};

constexpr std::uint32_t rotr(std::uint32_t value, std::uint32_t bits)
{
    return (value >> bits) | (value << (32 - bits));
}

void sha256Transform(Sha256Ctx &ctx, const std::uint8_t data[64])
{
    static constexpr std::uint32_t k[64] = {0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu,
        0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u,
        0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu,
        0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u,
        0xd5a79147u, 0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u,
        0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u,
        0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u,
        0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau,
        0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

    std::uint32_t m[64];
    for (std::uint32_t i = 0, j = 0; i < 16; ++i, j += 4)
        m[i] = (static_cast<std::uint32_t>(data[j]) << 24) | (static_cast<std::uint32_t>(data[j + 1]) << 16)
            | (static_cast<std::uint32_t>(data[j + 2]) << 8) | static_cast<std::uint32_t>(data[j + 3]);
    for (std::uint32_t i = 16; i < 64; ++i)
    {
        const std::uint32_t s0 = rotr(m[i - 15], 7) ^ rotr(m[i - 15], 18) ^ (m[i - 15] >> 3);
        const std::uint32_t s1 = rotr(m[i - 2], 17) ^ rotr(m[i - 2], 19) ^ (m[i - 2] >> 10);
        m[i] = m[i - 16] + s0 + m[i - 7] + s1;
    }

    std::uint32_t a = ctx.state[0];
    std::uint32_t b = ctx.state[1];
    std::uint32_t c = ctx.state[2];
    std::uint32_t d = ctx.state[3];
    std::uint32_t e = ctx.state[4];
    std::uint32_t f = ctx.state[5];
    std::uint32_t g = ctx.state[6];
    std::uint32_t h = ctx.state[7];
    for (std::uint32_t i = 0; i < 64; ++i)
    {
        const std::uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const std::uint32_t ch = (e & f) ^ (~e & g);
        const std::uint32_t temp1 = h + S1 + ch + k[i] + m[i];
        const std::uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temp2 = S0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }
    ctx.state[0] += a;
    ctx.state[1] += b;
    ctx.state[2] += c;
    ctx.state[3] += d;
    ctx.state[4] += e;
    ctx.state[5] += f;
    ctx.state[6] += g;
    ctx.state[7] += h;
}

void sha256Update(Sha256Ctx &ctx, const std::uint8_t *data, std::size_t len)
{
    for (std::size_t i = 0; i < len; ++i)
    {
        ctx.data[ctx.datalen++] = data[i];
        if (ctx.datalen == 64)
        {
            sha256Transform(ctx, ctx.data);
            ctx.bitlen += 512;
            ctx.datalen = 0;
        }
    }
}

void sha256Final(Sha256Ctx &ctx, std::uint8_t hash[32])
{
    std::uint32_t i = ctx.datalen;
    if (ctx.datalen < 56)
    {
        ctx.data[i++] = 0x80;
        while (i < 56)
            ctx.data[i++] = 0x00;
    }
    else
    {
        ctx.data[i++] = 0x80;
        while (i < 64)
            ctx.data[i++] = 0x00;
        sha256Transform(ctx, ctx.data);
        std::memset(ctx.data, 0, 56);
    }
    ctx.bitlen += static_cast<std::uint64_t>(ctx.datalen) * 8;
    ctx.data[63] = static_cast<std::uint8_t>(ctx.bitlen);
    ctx.data[62] = static_cast<std::uint8_t>(ctx.bitlen >> 8);
    ctx.data[61] = static_cast<std::uint8_t>(ctx.bitlen >> 16);
    ctx.data[60] = static_cast<std::uint8_t>(ctx.bitlen >> 24);
    ctx.data[59] = static_cast<std::uint8_t>(ctx.bitlen >> 32);
    ctx.data[58] = static_cast<std::uint8_t>(ctx.bitlen >> 40);
    ctx.data[57] = static_cast<std::uint8_t>(ctx.bitlen >> 48);
    ctx.data[56] = static_cast<std::uint8_t>(ctx.bitlen >> 56);
    sha256Transform(ctx, ctx.data);
    for (i = 0; i < 4; ++i)
    {
        hash[i] = (ctx.state[0] >> (24 - i * 8)) & 0xff;
        hash[i + 4] = (ctx.state[1] >> (24 - i * 8)) & 0xff;
        hash[i + 8] = (ctx.state[2] >> (24 - i * 8)) & 0xff;
        hash[i + 12] = (ctx.state[3] >> (24 - i * 8)) & 0xff;
        hash[i + 16] = (ctx.state[4] >> (24 - i * 8)) & 0xff;
        hash[i + 20] = (ctx.state[5] >> (24 - i * 8)) & 0xff;
        hash[i + 24] = (ctx.state[6] >> (24 - i * 8)) & 0xff;
        hash[i + 28] = (ctx.state[7] >> (24 - i * 8)) & 0xff;
    }
}

std::string toHex(const std::uint8_t *data, std::size_t size)
{
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < size; ++i)
        out << std::setw(2) << static_cast<int>(data[i]);
    return out.str();
}

} // namespace

bool sha256Bytes(const void *data, std::size_t size, std::string &hex, std::string &error)
{
    (void)error;
    Sha256Ctx ctx;
    sha256Update(ctx, static_cast<const std::uint8_t *>(data), size);
    std::uint8_t digest[32];
    sha256Final(ctx, digest);
    hex = toHex(digest, sizeof(digest));
    return true;
}

bool sha256File(const std::filesystem::path &path, std::string &hex, std::string &error)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        error = "could not hash file";
        return false;
    }
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return sha256Bytes(bytes.data(), bytes.size(), hex, error);
}
