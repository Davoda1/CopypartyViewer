// Copyparty Viewer 2.0.0 — Copyright (C) 2026 davoda1
// SPDX-License-Identifier: MIT
#include "preview.hpp"
#include <png.h>
#include <jpeglib.h>
#include <webp/decode.h>
#include <webp/demux.h>
#include <nanosvg.h>
#include <nanosvgrast.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <csetjmp>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace cpv {
namespace {
constexpr std::uint64_t max_pixels = 64ull * 1024 * 1024;
constexpr std::size_t max_input = 64u * 1024u * 1024u;
constexpr std::size_t max_text = 8u * 1024u * 1024u;
constexpr std::uint32_t max_dimension = 32768;
[[noreturn]] void invalid(const std::string& detail) {
    throw std::runtime_error("Cannot preview image: " + detail + ". You can download it or open it in your browser.");
}
std::string lower(std::string value) {
    for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}
std::string extension(const std::string& name) {
    auto dot = name.find_last_of('.');
    auto slash = name.find_last_of("/\\");
    return dot == std::string::npos || (slash != std::string::npos && dot < slash) ? "" : lower(name.substr(dot + 1));
}
std::string mime(const std::string& value) { return lower(value.substr(0, value.find(';'))); }
void dimensions(std::uint64_t w, std::uint64_t h) {
    if (!w || !h || w > max_dimension || h > max_dimension || w * h > max_pixels)
        invalid("dimensions exceed the 64 megapixel / 32768 pixel preview limit");
}
ImageData canvas(std::uint32_t w, std::uint32_t h, const std::string& label) {
    dimensions(w, h);
    ImageData result;
    result.width = static_cast<int>(w); result.height = static_cast<int>(h); result.channels = 4;
    result.pixels.resize(static_cast<std::size_t>(w) * h * 4);
    result.description = label + " · " + std::to_string(w) + " × " + std::to_string(h);
    return result;
}
struct Bytes {
    const unsigned char* data; std::size_t size;
    void require(std::size_t at, std::size_t n) const {
        if (at > size || n > size - at) invalid("truncated or malformed data");
    }
    unsigned u8(std::size_t at) const { require(at, 1); return data[at]; }
    std::uint16_t u16(std::size_t at) const { require(at, 2); return static_cast<std::uint16_t>(data[at] | (data[at+1] << 8)); }
    std::uint32_t u32(std::size_t at) const { require(at, 4); return data[at] | (std::uint32_t(data[at+1]) << 8) | (std::uint32_t(data[at+2]) << 16) | (std::uint32_t(data[at+3]) << 24); }
    std::int32_t s32(std::size_t at) const { return static_cast<std::int32_t>(u32(at)); }
};
bool signature(Bytes b, std::size_t offset, const char* value, std::size_t n) {
    return offset <= b.size && n <= b.size - offset && std::memcmp(b.data + offset, value, n) == 0;
}
ImageData png_decode(Bytes b) {
    png_image p{}; p.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&p, b.data, b.size)) { png_image_free(&p); invalid("invalid PNG data"); }
    try { dimensions(p.width, p.height); } catch (...) { png_image_free(&p); throw; }
    ImageData result;
    try { result = canvas(p.width, p.height, "PNG"); } catch (...) { png_image_free(&p); throw; }
    p.format = PNG_FORMAT_RGBA;
    if (!png_image_finish_read(&p, nullptr, result.pixels.data(), 0, nullptr)) {
        png_image_free(&p); invalid("invalid or incomplete PNG data");
    }
    png_image_free(&p);
    return result;
}
struct JpegError { jpeg_error_mgr base; std::jmp_buf jump; };
void jpeg_error(j_common_ptr info) { std::longjmp(reinterpret_cast<JpegError*>(info->err)->jump, 1); }
void jpeg_quiet(j_common_ptr) {}
void jpeg_message(j_common_ptr info, int level) { if (level < 0) jpeg_error(info); }
ImageData jpeg_decode(Bytes b) {
    jpeg_decompress_struct info{}; JpegError err{};
    volatile bool created = false;
    unsigned char* volatile pixels = nullptr;
    info.err = jpeg_std_error(&err.base);
    err.base.error_exit = jpeg_error; err.base.output_message = jpeg_quiet; err.base.emit_message = jpeg_message;
    if (setjmp(err.jump)) {
        if (created) jpeg_destroy_decompress(&info);
        std::free(pixels);
        invalid("invalid or incomplete JPEG data");
    }
    jpeg_create_decompress(&info); created = true;
    jpeg_mem_src(&info, b.data, static_cast<unsigned long>(b.size));
    jpeg_read_header(&info, TRUE);
    try { dimensions(info.image_width, info.image_height); } catch (...) { jpeg_destroy_decompress(&info); throw; }
    const bool cmyk = info.jpeg_color_space == JCS_CMYK || info.jpeg_color_space == JCS_YCCK;
    info.out_color_space = cmyk ? JCS_CMYK : JCS_RGB;
    jpeg_start_decompress(&info);
    const auto stride = static_cast<std::size_t>(info.output_width) * info.output_components;
    const auto size = stride * info.output_height;
    pixels = static_cast<unsigned char*>(std::malloc(size));
    if (!pixels) { jpeg_destroy_decompress(&info); throw std::bad_alloc(); }
    while (info.output_scanline < info.output_height) {
        JSAMPROW row = pixels + static_cast<std::size_t>(info.output_scanline) * stride;
        if (jpeg_read_scanlines(&info, &row, 1) != 1) jpeg_error(reinterpret_cast<j_common_ptr>(&info));
    }
    const auto w = info.output_width, h = info.output_height;
    const bool adobe = info.saw_Adobe_marker != 0;
    jpeg_finish_decompress(&info); jpeg_destroy_decompress(&info); created = false;
    ImageData result;
    try { result = canvas(w, h, "JPEG"); } catch (...) { std::free(pixels); throw; }
    for (std::size_t i = 0; i < static_cast<std::size_t>(w) * h; ++i) {
        auto* dst = &result.pixels[i * 4];
        if (cmyk) {
            const auto* src = pixels + i * 4;
            for (int c = 0; c < 3; ++c)
                dst[c] = static_cast<unsigned char>(adobe ? (unsigned(src[c]) * src[3] + 127) / 255 : ((255u-src[c]) * (255u-src[3]) + 127) / 255);
        } else std::memcpy(dst, pixels + i * 3, 3);
        dst[3] = 255;
    }
    std::free(pixels);
    return result;
}
ImageData webp_decode(Bytes b) {
    WebPBitstreamFeatures f{};
    if (WebPGetFeatures(b.data, b.size, &f) != VP8_STATUS_OK) invalid("invalid WebP data");
    dimensions(f.width, f.height);
    ImageData result = canvas(f.width, f.height, f.has_animation ? "WebP · first frame" : "WebP");
    if (!f.has_animation) {
        if (!WebPDecodeRGBAInto(b.data, b.size, result.pixels.data(), result.pixels.size(), result.width * 4)) invalid("incomplete WebP data");
    } else {
        WebPData data{b.data, b.size}; WebPAnimDecoderOptions options{};
        if (!WebPAnimDecoderOptionsInit(&options)) invalid("WebP decoder initialization failed");
        options.color_mode = MODE_RGBA; options.use_threads = 0;
        std::unique_ptr<WebPAnimDecoder, decltype(&WebPAnimDecoderDelete)> decoder(WebPAnimDecoderNew(&data, &options), &WebPAnimDecoderDelete);
        if (!decoder) invalid("invalid animated WebP data");
        WebPAnimInfo info{};
        if (!WebPAnimDecoderGetInfo(decoder.get(), &info) || info.canvas_width != unsigned(result.width) || info.canvas_height != unsigned(result.height)) invalid("invalid WebP canvas");
        unsigned char* pixels = nullptr; int timestamp = 0;
        if (!WebPAnimDecoderGetNext(decoder.get(), &pixels, &timestamp) || !pixels) invalid("incomplete WebP first frame");
        std::memcpy(result.pixels.data(), pixels, result.pixels.size());
    }
    return result;
}
ImageData svg_decode(Bytes b) {
    // NanoSVG renders geometry only: it never fetches resources or executes scripts.
    // Input and path-count bounds limit memory/cost before rasterization. Compressed
    // SVG is intentionally not passed to FLTK's unbounded gzip convenience decoder.
    if (b.size > 4u * 1024u * 1024u) invalid("SVG exceeds the 4 MiB vector preview limit");
    std::string xml(reinterpret_cast<const char*>(b.data), b.size);
    if (xml.find('\0') != std::string::npos) invalid("SVG contains embedded NUL bytes");
    std::unique_ptr<NSVGimage, decltype(&nsvgDelete)> parsed(nsvgParse(xml.data(), "px", 96), &nsvgDelete);
    if (!parsed || !std::isfinite(parsed->width) || !std::isfinite(parsed->height) || parsed->width <= 0 || parsed->height <= 0 || parsed->width > 1e7f || parsed->height > 1e7f) invalid("invalid SVG dimensions");
    std::uint64_t points = 0, shapes = 0;
    for (const NSVGshape* shape = parsed->shapes; shape; shape = shape->next) {
        if (++shapes > 5000) invalid("SVG contains too many shapes");
        if (!std::isfinite(shape->strokeWidth) || std::abs(shape->strokeWidth) > 1000000) invalid("SVG has an invalid stroke width");
        for (const NSVGpath* path = shape->paths; path; path = path->next) {
            points += std::max(path->npts, 0);
            if (points > 100000) invalid("SVG contains too many control points");
            for (int point = 0; point < path->npts * 2; ++point)
                if (!std::isfinite(path->pts[point]) || std::abs(path->pts[point]) > 1000000) invalid("SVG contains invalid or extreme coordinates");
        }
    }
    const float scale = std::min(1.0f, 4096.0f / std::max(parsed->width, parsed->height));
    const int w = std::max(1, int(std::lround(parsed->width * scale)));
    const int h = std::max(1, int(std::lround(parsed->height * scale)));
    ImageData result = canvas(w, h, "SVG");
    std::unique_ptr<NSVGrasterizer, decltype(&nsvgDeleteRasterizer)> raster(nsvgCreateRasterizer(), &nsvgDeleteRasterizer);
    if (!raster) throw std::bad_alloc();
    nsvgRasterize(raster.get(), parsed.get(), 0, 0, scale, result.pixels.data(), w, h, w * 4);
    if (scale < 1) result.description += " · fitted to 4096 pixels";
    return result;
}

unsigned channel(std::uint32_t value, std::uint32_t mask) {
    if (!mask) return 0;
    unsigned shift = 0;
    while (!(mask & 1)) { mask >>= 1; ++shift; }
    if ((mask & (mask + 1)) != 0) invalid("non-contiguous bitmap channel mask");
    return static_cast<unsigned>(((std::uint64_t(value >> shift) & mask) * 255 + mask/2) / mask);
}
ImageData bmp_decode(Bytes b, bool icon, int expected_w = 0, int expected_h = 0) {
    const std::size_t dib = icon ? 0 : 14;
    if (!icon && !signature(b, 0, "BM", 2)) invalid("invalid BMP header");
    const std::uint32_t header = b.u32(dib);
    if (header != 12 && (header < 40 || header > 124)) invalid("unsupported BMP header");
    b.require(dib, header);
    const bool core = header == 12;
    const std::int64_t signed_w = core ? b.u16(dib+4) : b.s32(dib+4);
    std::int64_t signed_h = core ? b.u16(dib+6) : b.s32(dib+8);
    const auto planes = b.u16(dib+(core ? 8 : 12));
    const auto bits = b.u16(dib+(core ? 10 : 14));
    const auto compression = core ? 0 : b.u32(dib+16);
    if (planes != 1 || signed_w <= 0 || signed_h == 0 || (icon && signed_h < 0)) invalid("invalid bitmap dimensions");
    if (icon) { if (signed_h % 2) invalid("invalid ICO bitmap height"); signed_h /= 2; }
    const bool top_down = signed_h < 0;
    const auto w = static_cast<std::uint32_t>(signed_w), h = static_cast<std::uint32_t>(std::abs(signed_h));
    dimensions(w,h);
    if (icon && (w != unsigned(expected_w) || h != unsigned(expected_h))) invalid("ICO dimensions do not match its directory");
    if (bits != 1 && bits != 4 && bits != 8 && bits != 16 && bits != 24 && bits != 32) invalid("unsupported BMP color depth");
    if (compression > 3 && compression != 6) invalid("unsupported BMP compression");
    if ((compression == 1 && bits != 8) || (compression == 2 && bits != 4) || ((compression == 3 || compression == 6) && bits != 16 && bits != 32) || ((compression == 1 || compression == 2) && (top_down || icon))) invalid("invalid BMP compression combination");
    std::size_t position = dib + header;
    std::uint32_t rmask = bits == 16 ? 0x7c00 : 0xff0000, gmask = bits == 16 ? 0x3e0 : 0xff00, bmask = bits == 16 ? 0x1f : 0xff, amask = 0;
    if (compression == 3 || compression == 6) {
        const std::size_t masks = header >= 52 ? dib + 40 : position;
        rmask = b.u32(masks); gmask = b.u32(masks+4); bmask = b.u32(masks+8);
        if (header >= 56 || compression == 6) amask = b.u32(masks+12);
        if (header == 40) position += compression == 6 ? 16 : 12;
        const auto combined = rmask | gmask | bmask | amask;
        if (!rmask || !gmask || !bmask || (rmask & gmask) || (rmask & bmask) || (gmask & bmask) || (amask & (rmask | gmask | bmask)) || (bits == 16 && (combined >> 16))) invalid("invalid BMP channel masks");
        (void)channel(0,rmask); (void)channel(0,gmask); (void)channel(0,bmask); (void)channel(0,amask);
    }
    std::array<std::array<unsigned char,4>,256> palette{};
    unsigned colors = 0;
    if (bits <= 8) {
        const auto declared = core ? 0 : b.u32(dib+32);
        colors = declared ? declared : (1u << bits);
        if (colors > (1u << bits)) invalid("invalid BMP palette");
        const std::size_t step = core ? 3 : 4;
        b.require(position, static_cast<std::size_t>(colors) * step);
        for (unsigned i=0; i<colors; ++i) palette[i] = {static_cast<unsigned char>(b.u8(position+i*step+2)),static_cast<unsigned char>(b.u8(position+i*step+1)),static_cast<unsigned char>(b.u8(position+i*step)),255};
        position += colors * step;
    }
    const std::size_t offset = icon ? position : b.u32(10);
    if (offset < position || offset > b.size) invalid("invalid BMP pixel offset");
    ImageData result = canvas(w,h, icon ? "ICO" : "BMP");
    auto indexed = [&](int x, int y, unsigned index) {
        if (x < 0 || x >= int(w) || y < 0 || y >= int(h) || index >= colors) invalid("BMP pixel outside its palette or canvas");
        std::copy(palette[index].begin(),palette[index].end(),result.pixels.begin()+(static_cast<std::size_t>(y)*w+x)*4);
    };
    std::size_t end_pixels = offset;
    bool any_alpha = false;
    if (compression == 1 || compression == 2) {
        for (std::size_t i=0; i<static_cast<std::size_t>(w)*h; ++i) std::copy(palette[0].begin(),palette[0].end(),result.pixels.begin()+i*4);
        int x=0,y=int(h)-1; std::size_t p=offset; bool ended=false;
        while (p < b.size && !ended) {
            unsigned n=b.u8(p++), v=b.u8(p++);
            if (n) { for (unsigned i=0;i<n;++i) indexed(x++,y,bits==8 ? v : ((i&1) ? v&15 : v>>4)); }
            else if (v==0) { x=0; --y; }
            else if (v==1) ended=true;
            else if (v==2) { x+=b.u8(p++); y-=b.u8(p++); if(x>int(w)||y<0) invalid("invalid BMP RLE displacement"); }
            else {
                unsigned bytes=bits==8 ? v : (v+1)/2;
                b.require(p,bytes+(bytes&1));
                for(unsigned i=0;i<v;++i) indexed(x++,y,bits==8 ? b.u8(p+i) : ((i&1) ? b.u8(p+i/2)&15 : b.u8(p+i/2)>>4));
                p+=bytes+(bytes&1);
            }
        }
        if (!ended) invalid("incomplete BMP RLE data");
        end_pixels=p;
    } else {
        const std::size_t stride=((static_cast<std::size_t>(w)*bits+31)/32)*4;
        b.require(offset,stride*h); end_pixels=offset+stride*h;
        for (unsigned y=0;y<h;++y) {
            const std::size_t row=offset+(top_down ? y : h-1-y)*stride;
            for (unsigned x=0;x<w;++x) {
                auto* dst=&result.pixels[(static_cast<std::size_t>(y)*w+x)*4];
                if(bits<=8) {
                    unsigned idx= bits==8 ? b.u8(row+x) : bits==4 ? ((x&1) ? b.u8(row+x/2)&15 : b.u8(row+x/2)>>4) : ((b.u8(row+x/8)>>(7-(x&7)))&1);
                    indexed(x,y,idx);
                } else if(bits==24) { const auto pos=row+x*3; dst[0]=b.u8(pos+2);dst[1]=b.u8(pos+1);dst[2]=b.u8(pos);dst[3]=255; }
                else {
                    const auto value=bits==16 ? b.u16(row+x*2) : b.u32(row+x*4);
                    dst[0]=channel(value,rmask);dst[1]=channel(value,gmask);dst[2]=channel(value,bmask);
                    dst[3]=amask ? channel(value,amask) : (bits==32 && compression==0 ? value>>24 : 255);
                    if(dst[3]) any_alpha=true;
                }
            }
        }
    }
    if(bits==32 && compression==0 && !any_alpha)
        for(std::size_t p=3;p<result.pixels.size();p+=4) result.pixels[p]=255;
    if(icon) {
        const std::size_t stride=((static_cast<std::size_t>(w)+31)/32)*4;
        if(end_pixels <= b.size && stride*h <= b.size-end_pixels) {
            for(unsigned y=0;y<h;++y) for(unsigned x=0;x<w;++x)
                if((b.u8(end_pixels+(h-1-y)*stride+x/8)>>(7-(x&7)))&1) result.pixels[(static_cast<std::size_t>(y)*w+x)*4+3]=0;
        } else if(bits!=32 || !any_alpha) invalid("missing ICO transparency mask");
    }
    return result;
}
ImageData ico_decode(Bytes b) {
    if(b.u16(0)!=0 || b.u16(2)!=1) invalid("invalid ICO header");
    const auto count=b.u16(4);
    if(!count || count>256) invalid("invalid ICO image count");
    b.require(6,static_cast<std::size_t>(count)*16);
    std::size_t chosen=6; unsigned best=0, depth=0;
    for(unsigned i=0;i<count;++i) {
        auto p=6+i*16; unsigned w=b.u8(p)?b.u8(p):256,h=b.u8(p+1)?b.u8(p+1):256,d=b.u16(p+6);
        if(w*h>best || (w*h==best && d>depth)) { chosen=p;best=w*h;depth=d; }
    }
    auto size=b.u32(chosen+8),offset=b.u32(chosen+12);
    b.require(offset,size);
    if(offset<6u+count*16u || size<8) invalid("invalid ICO image offset");
    Bytes payload{b.data+offset,size};
    const auto w=b.u8(chosen)?b.u8(chosen):256,h=b.u8(chosen+1)?b.u8(chosen+1):256;
    auto result=signature(payload,0,"\x89PNG\r\n\x1a\n",8)?png_decode(payload):bmp_decode(payload,true,w,h);
    if(result.width!=int(w)||result.height!=int(h)) invalid("ICO dimensions do not match its directory");
    result.description="ICO · highest resolution · "+std::to_string(w)+" × "+std::to_string(h);
    return result;
}

ImageData gif_decode(Bytes b) {
    if(!signature(b,0,"GIF87a",6)&&!signature(b,0,"GIF89a",6)) invalid("invalid GIF header");
    auto w=b.u16(6),h=b.u16(8);dimensions(w,h);
    auto flags=b.u8(10); std::size_t p=13;
    using Palette=std::array<std::array<unsigned char,4>,256>;
    Palette global{};unsigned global_count=0;
    auto palette=[&](Palette& colors,unsigned n) {
        b.require(p,n*3);
        for(unsigned i=0;i<n;++i) colors[i]={static_cast<unsigned char>(b.u8(p+i*3)),static_cast<unsigned char>(b.u8(p+i*3+1)),static_cast<unsigned char>(b.u8(p+i*3+2)),255};
        p+=n*3;
    };
    if(flags&128) { global_count=2u<<(flags&7);palette(global,global_count); }
    int transparent=-1;
    auto blocks=[&]() {
        std::vector<unsigned char> bytes;
        for(;;) { auto n=b.u8(p++);if(!n)break;b.require(p,n);bytes.insert(bytes.end(),b.data+p,b.data+p+n);p+=n; }
        return bytes;
    };
    while(p<b.size) {
        auto marker=b.u8(p++);
        if(marker==0x3b) invalid("GIF has no image frame");
        if(marker==0x21) {
            auto label=b.u8(p++);
            if(label==0xf9) {
                if(b.u8(p++)!=4) invalid("invalid GIF graphics control");
                auto control=b.u8(p);transparent=(control&1)?int(b.u8(p+3)):-1;p+=4;
                if(b.u8(p++)!=0) invalid("invalid GIF graphics control terminator");
            } else (void)blocks();
            continue;
        }
        if(marker!=0x2c) invalid("invalid GIF block");
        auto left=b.u16(p),top=b.u16(p+2),fw=b.u16(p+4),fh=b.u16(p+6);auto frame_flags=b.u8(p+8);p+=9;
        if(!fw||!fh||std::uint32_t(left)+fw>w||std::uint32_t(top)+fh>h) invalid("GIF frame exceeds its canvas");
        Palette local=global;unsigned count=global_count;
        if(frame_flags&128) {count=2u<<(frame_flags&7);palette(local,count);}
        if(!count) invalid("missing GIF palette");
        unsigned min_code=b.u8(p++);if(min_code<2||min_code>8) invalid("invalid GIF LZW code size");
        auto compressed=blocks();std::size_t bit=0;
        unsigned clear=1u<<min_code, end=clear+1, next=end+1, code_bits=min_code+1;
        std::array<unsigned short,4096> prefix{};std::array<unsigned char,4096> suffix{},stack{};
        std::vector<unsigned char> decoded;decoded.reserve(static_cast<std::size_t>(fw)*fh);
        int previous=-1;unsigned char first=0;bool finished=false;
        auto read_code=[&]() {
            if(bit+code_bits>compressed.size()*8) invalid("truncated GIF LZW data");
            unsigned value=0;for(unsigned n=0;n<code_bits;++n) value|=((compressed[(bit+n)/8]>>((bit+n)&7))&1)<<n;
            bit+=code_bits;return value;
        };
        while(!finished) {
            unsigned code=read_code();
            if(code==clear) {next=end+1;code_bits=min_code+1;previous=-1;continue;}
            if(code==end) {finished=true;break;}
            const unsigned original=code;unsigned n=0;
            if(previous<0) {
                if(code>=clear) invalid("invalid initial GIF LZW code");
                first=static_cast<unsigned char>(code);stack[n++]=first;
            } else {
                if(code==next) {stack[n++]=first;code=static_cast<unsigned>(previous);}
                else if(code>next) invalid("invalid GIF LZW dictionary reference");
                unsigned traversed=0;
                while(code>=clear) {
                    if(code>=next||code>=4096||++traversed>4095||n>=4095) invalid("invalid GIF LZW dictionary");
                    stack[n++]=suffix[code];code=prefix[code];
                }
                first=static_cast<unsigned char>(code);stack[n++]=first;
                if(next<4096) {
                    prefix[next]=static_cast<unsigned short>(previous);suffix[next]=first;++next;
                    if(next==(1u<<code_bits)&&code_bits<12) ++code_bits;
                }
            }
            while(n) {
                if(decoded.size()>=static_cast<std::size_t>(fw)*fh) invalid("GIF contains excess frame pixels");
                decoded.push_back(stack[--n]);
            }
            previous=static_cast<int>(original);
        }
        if(!finished||decoded.size()!=static_cast<std::size_t>(fw)*fh) invalid("incomplete GIF frame");
        ImageData result=canvas(w,h,"GIF · first frame");
        // Transparent pixels outside a partial first frame are retained.
        if(transparent<0 && b.u8(11)<global_count)
            for(std::size_t i=0;i<static_cast<std::size_t>(w)*h;++i) std::copy(global[b.u8(11)].begin(),global[b.u8(11)].end(),result.pixels.begin()+i*4);
        std::vector<unsigned> rows;rows.reserve(fh);
        if(frame_flags&64) {
            const unsigned starts[]={0,4,2,1},steps[]={8,8,4,2};
            for(unsigned pass=0;pass<4;++pass) for(unsigned y=starts[pass];y<fh;y+=steps[pass]) rows.push_back(y);
        } else for(unsigned y=0;y<fh;++y) rows.push_back(y);
        for(unsigned y=0;y<fh;++y)for(unsigned x=0;x<fw;++x) {
            unsigned index=decoded[static_cast<std::size_t>(y)*fw+x];
            if(index>=count) invalid("GIF palette index out of range");
            auto* dst=&result.pixels[((static_cast<std::size_t>(top)+rows[y])*w+left+x)*4];
            if(int(index)==transparent) {dst[0]=dst[1]=dst[2]=dst[3]=0;}
            else std::copy(local[index].begin(),local[index].end(),dst);
        }
        return result;
    }
    invalid("incomplete GIF data");
}

bool in_list(const std::string& value, const char* const* values, std::size_t count) {
    for(std::size_t i=0;i<count;++i) if(value==values[i]) return true;
    return false;
}
// Number of bytes in a valid scalar; zero signals invalid UTF-8. Overlong,
// surrogate and out-of-range sequences are all rejected.
unsigned utf8_length(const unsigned char* p,std::size_t left) {
    if(!left)return 0;
    unsigned c=p[0];
    if(c<128)return 1;
    unsigned n=c>=0xc2&&c<=0xdf?2:c>=0xe0&&c<=0xef?3:c>=0xf0&&c<=0xf4?4:0;
    if(!n||left<n)return 0;
    for(unsigned i=1;i<n;++i)if((p[i]&0xc0)!=0x80)return 0;
    if((c==0xe0&&p[1]<0xa0)||(c==0xed&&p[1]>=0xa0)||(c==0xf0&&p[1]<0x90)||(c==0xf4&&p[1]>=0x90))return 0;
    return n;
}
} // namespace

bool image_supported(const std::string& filename,const std::string& content_type) {
    const auto ext=extension(filename),type=mime(content_type);
    const char* extensions[]={"png","jpg","jpeg","jpe","jfif","webp","svg","bmp","ico","gif"};
    const char* types[]={"image/png","image/jpeg","image/webp","image/svg+xml","image/bmp","image/x-ms-bmp","image/x-icon","image/vnd.microsoft.icon","image/gif"};
    return in_list(ext,extensions,std::size(extensions))||in_list(type,types,std::size(types));
}
ImageData decode_image(const std::vector<unsigned char>& bytes,const std::string& filename,const std::string& content_type) {
    if(bytes.empty())invalid("empty file");
    if(bytes.size()>max_input)invalid("file exceeds the 64 MiB preview limit");
    Bytes b{bytes.data(),bytes.size()};
    if(signature(b,0,"\x89PNG\r\n\x1a\n",8))return png_decode(b);
    if(b.size>=3&&b.data[0]==0xff&&b.data[1]==0xd8&&b.data[2]==0xff)return jpeg_decode(b);
    if(signature(b,0,"RIFF",4)&&signature(b,8,"WEBP",4))return webp_decode(b);
    if(signature(b,0,"BM",2))return bmp_decode(b,false);
    if(b.size>=6&&b.u16(0)==0&&b.u16(2)==1)return ico_decode(b);
    if(signature(b,0,"GIF87a",6)||signature(b,0,"GIF89a",6))return gif_decode(b);
    if(extension(filename)=="svg"||mime(content_type)=="image/svg+xml")return svg_decode(b);
    invalid("unsupported or unrecognized image format");
}

bool text_supported(const std::string& filename,const std::string& content_type,const std::vector<unsigned char>& bytes,bool allow_encoding_override) {
    const auto type=mime(content_type),ext=extension(filename);
    const char* extensions[]={"txt","md","markdown","mdown","mkd","mkdn","mdtext","mdtxt","log","csv","tsv","json","jsonc","json5","xml","html","htm","xhtml","css","scss","js","mjs","cjs","ts","jsx","tsx","c","cc","cpp","cxx","h","hpp","hxx","py","pyw","sh","bash","zsh","bat","cmd","ps1","psm1","psd1","ini","cfg","conf","toml","yaml","yml","sql","rs","go","java","cs","php","cmake","make","srt","vtt","nfo","desktop"};
    const bool known=type.rfind("text/",0)==0 || type=="application/json" || type=="application/xml" || type=="application/javascript" || in_list(ext,extensions,std::size(extensions));
    const std::size_t count=std::min<std::size_t>(bytes.size(),8192);
    unsigned controls=0,invalid_utf8=0;
    for(std::size_t i=0;i<count;) {
        // A recognized text format can still be BOM-less UTF-16/32 that Auto
        // cannot identify. Let the UI offer its explicit encoding selector;
        // the source preparer removes control characters before display.
        unsigned c=bytes[i];if(c==0)return allow_encoding_override&&known;
        if(c<32&&c!='\t'&&c!='\r'&&c!='\n'&&c!=27&&c!='\f'&&c!='\b')++controls;
        unsigned n=utf8_length(bytes.data()+i,bytes.size()-i);
        if(!n){++invalid_utf8;++i;}else i+=n;
    }
    if(controls>count/100+1)return allow_encoding_override&&known;
    return known || (invalid_utf8==0 && (type.empty()||type=="application/octet-stream"||type=="binary/octet-stream"));
}

PreparedText prepare_styled_text(const std::vector<unsigned char>& bytes) {
    PreparedText result;result.text.reserve(std::min(bytes.size(),max_text));result.styles.reserve(std::min(bytes.size(),max_text));
    bool bold=false;int foreground=-1;
    auto style=[&]() {return foreground<0 ? (bold?'B':'A') : static_cast<char>((bold?'K':'C')+foreground);};
    auto append=[&](const char* s,std::size_t n) {result.text.append(s,n);result.styles.append(n,style());};
    auto sgr=[&](std::string_view args) {
        std::vector<int> values;int value=0;bool have=false;
        for(char c:args) {
            if(c>='0'&&c<='9'){value=std::min(100000,value*10+c-'0');have=true;}
            else if(c==';'||c==':'){values.push_back(have?value:0);value=0;have=false;}
            else return;
        }
        values.push_back(have?value:0);
        for(std::size_t k=0;k<values.size();++k) {
            int v=values[k];
            if(v==0){bold=false;foreground=-1;}
            else if(v==1)bold=true;
            else if(v==22)bold=false;
            else if(v>=30&&v<=37)foreground=v-30;
            else if(v==39)foreground=-1;
            else if(v>=90&&v<=97){foreground=v-90;bold=true;}
            else if((v==38||v==48)&&k+1<values.size()) {
                // Consume extended color parameters so their numeric values cannot
                // accidentally enable bold; map foreground to the nearest ANSI color.
                int r=0,g=0,b=0;bool color=false;
                if(values[k+1]==5&&k+2<values.size()) {
                    int n=std::clamp(values[k+2],0,255);k+=2;
                    if(n<16){if(v==38){foreground=n%8;bold=n>=8;}continue;}
                    if(n<232){n-=16;r=(n/36)?55+(n/36)*40:0;g=((n/6)%6)?55+((n/6)%6)*40:0;b=(n%6)?55+(n%6)*40:0;}else r=g=b=8+(n-232)*10;
                    color=true;
                } else if(values[k+1]==2&&k+4<values.size()) {r=std::clamp(values[k+2],0,255);g=std::clamp(values[k+3],0,255);b=std::clamp(values[k+4],0,255);k+=4;color=true;}
                if(v==38&&color) foreground=(r>=128?1:0)|(g>=128?2:0)|(b>=128?4:0);
            }
        }
    };
    std::size_t i=0,limit=std::min(bytes.size(),max_text);
    if(limit>=3&&bytes[0]==0xef&&bytes[1]==0xbb&&bytes[2]==0xbf)i=3;
    while(i<limit) {
        const unsigned char c=bytes[i];
        if(c==27) {
            ++i;if(i>=limit)break;
            if(bytes[i]=='[') {
                std::size_t start=++i;
                while(i<limit&&!(bytes[i]>=0x40&&bytes[i]<=0x7e))++i;
                if(i<limit){if(bytes[i]=='m'&&i-start<=256)sgr(std::string_view(reinterpret_cast<const char*>(bytes.data()+start),i-start));++i;}
            } else if(bytes[i]==']'||bytes[i]=='P'||bytes[i]=='_'||bytes[i]=='^') {
                ++i;while(i<limit){if(bytes[i]==7){++i;break;}if(bytes[i]==27&&i+1<limit&&bytes[i+1]=='\\'){i+=2;break;}++i;}
            } else { if(bytes[i]>=0x20&&bytes[i]<=0x2f){while(i<limit&&bytes[i]>=0x20&&bytes[i]<=0x2f)++i;}if(i<limit)++i; }
            continue;
        }
        if(c=='\r'){if(i+1<limit&&bytes[i+1]=='\n')++i;append("\n",1);++i;continue;}
        if(c=='\n'||c=='\t'){append(reinterpret_cast<const char*>(&bytes[i]),1);++i;continue;}
        if(c<32||c==127){++i;continue;}
        const auto n=utf8_length(bytes.data()+i,limit-i);
        if(n){append(reinterpret_cast<const char*>(bytes.data()+i),n);i+=n;}else{append("\xef\xbf\xbd",3);++i;}
    }
    if(bytes.size()>limit){bold=false;foreground=-1;const std::string note="\n\n[Preview truncated at 8 MiB. Download the file to read it in full.]\n";append(note.data(),note.size());}
    return result;
}
std::string prepare_text(const std::vector<unsigned char>& bytes) {return prepare_styled_text(bytes).text;}
} // namespace cpv
