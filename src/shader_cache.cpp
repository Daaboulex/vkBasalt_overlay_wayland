#include "shader_cache.hpp"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <list>
#include <mutex>
#include <queue>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

#include <sys/stat.h>
#include <unistd.h>

#include "reshade/effect_preprocessor.hpp"
#include "reshade/effect_parser.hpp"
#include "reshade/effect_codegen.hpp"

#include "logger.hpp"
#include "reshade_fx_env.hpp"

namespace vkBasalt
{
    namespace
    {
        // Bump when the serialized layout, base macros, or stubs change.
        constexpr uint32_t SCHEMA_VERSION = 4;
        constexpr uint32_t MAGIC = 0x43424B56; // "VKBC"
        constexpr size_t MEMORY_CACHE_CAP = 16;
        constexpr size_t DISK_CACHE_CAP = 256;
        constexpr uint32_t MAX_COUNT = 1u << 24;

        uint64_t fnv1a(const std::string& data, uint64_t hash)
        {
            for (unsigned char c : data)
            {
                hash ^= c;
                hash *= 1099511628211ull;
            }
            return hash;
        }

        std::string hashName(const std::string& blob)
        {
            uint64_t a = fnv1a(blob, 14695981039346656037ull);
            uint64_t b = fnv1a(blob, 1099511628211ull * 31 + 7);
            char buf[33];
            snprintf(buf, sizeof(buf), "%016llx%016llx", (unsigned long long)a, (unsigned long long)b);
            return buf;
        }

        uint64_t contentHash(const std::string& data)
        {
            return fnv1a(data, 14695981039346656037ull);
        }

        bool readFileBytes(const std::string& path, std::string& out)
        {
            std::ifstream f(path, std::ios::binary);
            if (!f.is_open())
                return false;
            std::ostringstream ss;
            ss << f.rdbuf();
            out = ss.str();
            return !f.bad();
        }

        struct Writer
        {
            std::string buf;

            void u8(uint8_t v) { buf.push_back((char)v); }
            void u32(uint32_t v) { buf.append((const char*)&v, 4); }
            void u64(uint64_t v) { buf.append((const char*)&v, 8); }
            void i32(int32_t v) { buf.append((const char*)&v, 4); }
            void f32(float v) { buf.append((const char*)&v, 4); }
            void str(const std::string& s)
            {
                u32((uint32_t)s.size());
                buf.append(s);
            }
            void bytes(const void* p, size_t n) { buf.append((const char*)p, n); }
        };

        struct Reader
        {
            const std::string& buf;
            size_t pos = 0;

            explicit Reader(const std::string& b) : buf(b) {}

            void need(size_t n)
            {
                if (pos + n > buf.size())
                    throw std::runtime_error("cache entry truncated");
            }
            uint8_t u8()
            {
                need(1);
                return (uint8_t)buf[pos++];
            }
            uint32_t u32()
            {
                need(4);
                uint32_t v;
                std::memcpy(&v, buf.data() + pos, 4);
                pos += 4;
                return v;
            }
            uint64_t u64()
            {
                need(8);
                uint64_t v;
                std::memcpy(&v, buf.data() + pos, 8);
                pos += 8;
                return v;
            }
            int32_t i32()
            {
                need(4);
                int32_t v;
                std::memcpy(&v, buf.data() + pos, 4);
                pos += 4;
                return v;
            }
            float f32()
            {
                need(4);
                float v;
                std::memcpy(&v, buf.data() + pos, 4);
                pos += 4;
                return v;
            }
            uint32_t count()
            {
                uint32_t n = u32();
                if (n > MAX_COUNT)
                    throw std::runtime_error("cache entry count out of range");
                return n;
            }
            std::string str()
            {
                uint32_t n = count();
                need(n);
                std::string s = buf.substr(pos, n);
                pos += n;
                return s;
            }
            void bytes(void* p, size_t n)
            {
                need(n);
                std::memcpy(p, buf.data() + pos, n);
                pos += n;
            }
        };

        void putType(Writer& w, const reshadefx::type& t)
        {
            w.u8((uint8_t)t.base);
            w.u32(t.rows);
            w.u32(t.cols);
            w.u32(t.qualifiers);
            w.i32(t.array_length);
            w.u32(t.definition);
        }

        reshadefx::type getType(Reader& r)
        {
            reshadefx::type t;
            t.base = (reshadefx::type::datatype)r.u8();
            t.rows = r.u32();
            t.cols = r.u32();
            t.qualifiers = r.u32();
            t.array_length = r.i32();
            t.definition = r.u32();
            return t;
        }

        void putConstant(Writer& w, const reshadefx::constant& c)
        {
            w.bytes(c.as_uint, sizeof(c.as_uint));
            w.str(c.string_data);
            w.u32((uint32_t)c.array_data.size());
            for (const auto& e : c.array_data)
                putConstant(w, e);
        }

        reshadefx::constant getConstant(Reader& r)
        {
            reshadefx::constant c;
            r.bytes(c.as_uint, sizeof(c.as_uint));
            c.string_data = r.str();
            uint32_t n = r.count();
            c.array_data.resize(n);
            for (uint32_t i = 0; i < n; i++)
                c.array_data[i] = getConstant(r);
            return c;
        }

        void putAnnotations(Writer& w, const std::vector<reshadefx::annotation>& v)
        {
            w.u32((uint32_t)v.size());
            for (const auto& a : v)
            {
                putType(w, a.type);
                w.str(a.name);
                putConstant(w, a.value);
            }
        }

        std::vector<reshadefx::annotation> getAnnotations(Reader& r)
        {
            uint32_t n = r.count();
            std::vector<reshadefx::annotation> v(n);
            for (uint32_t i = 0; i < n; i++)
            {
                v[i].type = getType(r);
                v[i].name = r.str();
                v[i].value = getConstant(r);
            }
            return v;
        }

        void putModule(Writer& w, const reshadefx::module& m)
        {
            w.str(m.hlsl);

            w.u32((uint32_t)m.spirv.size());
            w.bytes(m.spirv.data(), m.spirv.size() * sizeof(uint32_t));

            w.u32((uint32_t)m.entry_points.size());
            for (const auto& e : m.entry_points)
            {
                w.str(e.name);
                w.u8((uint8_t) e.type);
            }

            w.u32((uint32_t)m.textures.size());
            for (const auto& t : m.textures)
            {
                w.u32(t.id);
                w.u32(t.binding);
                w.str(t.semantic);
                w.str(t.unique_name);
                putAnnotations(w, t.annotations);
                w.u32(t.width);
                w.u32(t.height);
                w.u32(t.levels);
                w.u32((uint32_t)t.format);
            }

            w.u32((uint32_t)m.samplers.size());
            for (const auto& s : m.samplers)
            {
                w.u32(s.id);
                w.u32(s.binding);
                w.u32(s.texture_binding);
                w.str(s.unique_name);
                w.str(s.texture_name);
                putAnnotations(w, s.annotations);
                w.u32((uint32_t)s.filter);
                w.u32((uint32_t)s.address_u);
                w.u32((uint32_t)s.address_v);
                w.u32((uint32_t)s.address_w);
                w.f32(s.min_lod);
                w.f32(s.max_lod);
                w.f32(s.lod_bias);
                w.u8(s.srgb);
            }

            auto putUniforms = [&](const std::vector<reshadefx::uniform_info>& v) {
                w.u32((uint32_t)v.size());
                for (const auto& u : v)
                {
                    w.str(u.name);
                    putType(w, u.type);
                    w.u32(u.size);
                    w.u32(u.offset);
                    putAnnotations(w, u.annotations);
                    w.u8(u.has_initializer_value ? 1 : 0);
                    putConstant(w, u.initializer_value);
                }
            };
            putUniforms(m.uniforms);
            putUniforms(m.spec_constants);

            w.u32((uint32_t)m.techniques.size());
            for (const auto& t : m.techniques)
            {
                w.str(t.name);
                putAnnotations(w, t.annotations);
                w.u32((uint32_t)t.passes.size());
                for (const auto& p : t.passes)
                {
                    for (int i = 0; i < 8; i++)
                        w.str(p.render_target_names[i]);
                    w.str(p.vs_entry_point);
                    w.str(p.ps_entry_point);
                    w.str(p.cs_entry_point);
                    w.u8(p.clear_render_targets);
                    w.u8(p.srgb_write_enable);
                    w.u8(p.blend_enable);
                    w.u8(p.stencil_enable);
                    w.u8(p.color_write_mask);
                    w.u8(p.stencil_read_mask);
                    w.u8(p.stencil_write_mask);
                    w.u8((uint8_t)p.blend_op);
                    w.u8((uint8_t)p.blend_op_alpha);
                    w.u8((uint8_t)p.src_blend);
                    w.u8((uint8_t)p.dest_blend);
                    w.u8((uint8_t)p.src_blend_alpha);
                    w.u8((uint8_t)p.dest_blend_alpha);
                    w.u8((uint8_t)p.stencil_comparison_func);
                    w.u32(p.stencil_reference_value);
                    w.u8((uint8_t)p.stencil_op_pass);
                    w.u8((uint8_t)p.stencil_op_fail);
                    w.u8((uint8_t)p.stencil_op_depth_fail);
                    w.u32(p.num_vertices);
                    w.u8((uint8_t)p.topology);
                    w.u32(p.viewport_width);
                    w.u32(p.viewport_height);
                    w.u32(p.dispatch_z);
                }
            }

            w.u32(m.total_uniform_size);
            w.u32(m.num_sampler_bindings);
            w.u32(m.num_texture_bindings);
        }

        reshadefx::module getModule(Reader& r)
        {
            reshadefx::module m;
            m.hlsl = r.str();

            uint32_t n = r.count();
            m.spirv.resize(n);
            r.bytes(m.spirv.data(), n * sizeof(uint32_t));

            n = r.count();
            m.entry_points.resize(n);
            for (auto& e : m.entry_points)
            {
                e.name = r.str();
                e.type = (reshadefx::shader_type) r.u8();
            }

            n = r.count();
            m.textures.resize(n);
            for (auto& t : m.textures)
            {
                t.id = r.u32();
                t.binding = r.u32();
                t.semantic = r.str();
                t.unique_name = r.str();
                t.annotations = getAnnotations(r);
                t.width = r.u32();
                t.height = r.u32();
                t.levels = r.u32();
                t.format = (reshadefx::texture_format)r.u32();
            }

            n = r.count();
            m.samplers.resize(n);
            for (auto& s : m.samplers)
            {
                s.id = r.u32();
                s.binding = r.u32();
                s.texture_binding = r.u32();
                s.unique_name = r.str();
                s.texture_name = r.str();
                s.annotations = getAnnotations(r);
                s.filter = (reshadefx::texture_filter)r.u32();
                s.address_u = (reshadefx::texture_address_mode)r.u32();
                s.address_v = (reshadefx::texture_address_mode)r.u32();
                s.address_w = (reshadefx::texture_address_mode)r.u32();
                s.min_lod = r.f32();
                s.max_lod = r.f32();
                s.lod_bias = r.f32();
                s.srgb = r.u8();
            }

            auto getUniforms = [&](std::vector<reshadefx::uniform_info>& v) {
                uint32_t k = r.count();
                v.resize(k);
                for (auto& u : v)
                {
                    u.name = r.str();
                    u.type = getType(r);
                    u.size = r.u32();
                    u.offset = r.u32();
                    u.annotations = getAnnotations(r);
                    u.has_initializer_value = r.u8() != 0;
                    u.initializer_value = getConstant(r);
                }
            };
            getUniforms(m.uniforms);
            getUniforms(m.spec_constants);

            n = r.count();
            m.techniques.resize(n);
            for (auto& t : m.techniques)
            {
                t.name = r.str();
                t.annotations = getAnnotations(r);
                uint32_t pc = r.count();
                t.passes.resize(pc);
                for (auto& p : t.passes)
                {
                    for (int i = 0; i < 8; i++)
                        p.render_target_names[i] = r.str();
                    p.vs_entry_point = r.str();
                    p.ps_entry_point = r.str();
                    p.cs_entry_point = r.str();
                    p.clear_render_targets = r.u8();
                    p.srgb_write_enable = r.u8();
                    p.blend_enable = r.u8();
                    p.stencil_enable = r.u8();
                    p.color_write_mask = r.u8();
                    p.stencil_read_mask = r.u8();
                    p.stencil_write_mask = r.u8();
                    p.blend_op = (reshadefx::pass_blend_op)r.u8();
                    p.blend_op_alpha = (reshadefx::pass_blend_op)r.u8();
                    p.src_blend = (reshadefx::pass_blend_func)r.u8();
                    p.dest_blend = (reshadefx::pass_blend_func)r.u8();
                    p.src_blend_alpha = (reshadefx::pass_blend_func)r.u8();
                    p.dest_blend_alpha = (reshadefx::pass_blend_func)r.u8();
                    p.stencil_comparison_func = (reshadefx::pass_stencil_func)r.u8();
                    p.stencil_reference_value = r.u32();
                    p.stencil_op_pass = (reshadefx::pass_stencil_op)r.u8();
                    p.stencil_op_fail = (reshadefx::pass_stencil_op)r.u8();
                    p.stencil_op_depth_fail = (reshadefx::pass_stencil_op)r.u8();
                    p.num_vertices = r.u32();
                    p.topology = (reshadefx::primitive_topology)r.u8();
                    p.viewport_width = r.u32();
                    p.viewport_height = r.u32();
                    p.dispatch_z = r.u32();
                }
            }

            m.total_uniform_size = r.u32();
            m.num_sampler_bindings = r.u32();
            m.num_texture_bindings = r.u32();
            return m;
        }

        std::string serializeEntry(const CompiledReshadeEffect& e)
        {
            Writer w;
            w.u32(MAGIC);
            w.u32(SCHEMA_VERSION);

            w.u32((uint32_t)e.includedFiles.size());
            for (const auto& [path, hash] : e.includedFiles)
            {
                w.str(path);
                w.u64(hash);
            }

            w.str(e.error);
            if (!e.error.empty())
                return w.buf;

            w.u32((uint32_t)e.usedMacros.size());
            for (const auto& [name, value] : e.usedMacros)
            {
                w.str(name);
                w.str(value);
            }
            w.u8(e.usesDepth ? 1 : 0);
            w.str(e.warnings);
            putModule(w, e.module);
            return w.buf;
        }

        std::shared_ptr<CompiledReshadeEffect> deserializeEntry(const std::string& buf)
        {
            Reader r(buf);
            if (r.u32() != MAGIC || r.u32() != SCHEMA_VERSION)
                throw std::runtime_error("cache entry version mismatch");

            auto e = std::make_shared<CompiledReshadeEffect>();

            uint32_t n = r.count();
            e->includedFiles.resize(n);
            for (auto& [path, hash] : e->includedFiles)
            {
                path = r.str();
                hash = r.u64();
            }

            e->error = r.str();
            if (!e->error.empty())
                return e;

            n = r.count();
            e->usedMacros.resize(n);
            for (auto& [name, value] : e->usedMacros)
            {
                name = r.str();
                value = r.str();
            }
            e->usesDepth = r.u8() != 0;
            e->warnings = r.str();
            e->module = getModule(r);
            return e;
        }

        std::string cacheDir()
        {
            const char* xdg = std::getenv("XDG_CACHE_HOME");
            if (xdg && *xdg)
                return std::string(xdg) + "/vkBasalt-overlay";
            const char* home = std::getenv("HOME");
            if (home && *home)
                return std::string(home) + "/.cache/vkBasalt-overlay";
            return "";
        }

        bool ensureCacheDir(const std::string& dir)
        {
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            return !ec && std::filesystem::is_directory(dir, ec);
        }

        void pruneDiskCache(const std::string& dir)
        {
            std::error_code ec;
            std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> entries;
            for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
            {
                if (entry.path().extension() != ".vkbfx")
                    continue;
                std::error_code tec;
                auto t = entry.last_write_time(tec);
                if (!tec)
                    entries.push_back({t, entry.path()});
            }
            if (entries.size() <= DISK_CACHE_CAP)
                return;

            std::sort(entries.begin(), entries.end());
            size_t excess = entries.size() - DISK_CACHE_CAP * 7 / 8;
            for (size_t i = 0; i < excess; i++)
            {
                std::error_code uec;
                std::filesystem::remove(entries[i].second, uec);
            }
            Logger::debug("shader cache: pruned " + std::to_string(excess) + " old entries");
        }

        bool writeEntryFile(const std::string& dir, const std::string& key, const std::string& payload)
        {
            std::string finalPath = dir + "/" + key + ".vkbfx";
            std::string tmpPath = finalPath + "." + std::to_string(getpid()) + ".tmp";

            std::ofstream f(tmpPath, std::ios::binary | std::ios::trunc);
            if (!f.is_open())
                return false;
            f.write(payload.data(), (std::streamsize)payload.size());
            f.close();
            if (f.fail())
            {
                std::remove(tmpPath.c_str());
                return false;
            }
            if (std::rename(tmpPath.c_str(), finalPath.c_str()) != 0)
            {
                std::remove(tmpPath.c_str());
                return false;
            }
            return true;
        }

        bool includedFilesUnchanged(const CompiledReshadeEffect& e)
        {
            for (const auto& [path, hash] : e.includedFiles)
            {
                std::string content;
                if (!readFileBytes(path, content))
                    return false;
                if (contentHash(content) != hash)
                    return false;
            }
            return true;
        }


        // A shader counts as depth-using only when an entry point reaches a
        // DEPTH-semantic sampler in the SPIR-V; a raw OpLoad scan false-positives
        // on unreachable helpers that ReShade.fxh declares.
        bool moduleUsesDepth(const reshadefx::module& module)
        {
            std::string depthTexName;
            for (const auto& tex : module.textures)
            {
                if (tex.semantic == "DEPTH")
                {
                    depthTexName = tex.unique_name;
                    break;
                }
            }
            if (depthTexName.empty())
                return false;

            auto isSamplerUsedInSpirv = [&](uint32_t samplerId) -> bool {
                const auto& code = module.spirv;
                if (code.size() < 5)
                    return false;

                std::set<uint32_t> entryFuncIds;
                size_t i = 5;
                while (i < code.size())
                {
                    uint32_t wc = code[i] >> 16;
                    uint32_t op = code[i] & 0xFFFF;
                    if (wc == 0)
                        break;
                    // OpEntryPoint: [wc|15, execModel, funcId, name..., interface...]
                    if (op == 15 && wc >= 3 && (i + 2) < code.size())
                        entryFuncIds.insert(code[i + 2]);
                    i += wc;
                }

                struct FuncInfo
                {
                    bool loadsDepthSampler = false;
                    std::vector<uint32_t> callees;
                };
                std::unordered_map<uint32_t, FuncInfo> funcs;
                uint32_t currentFunc = 0;
                i = 5;
                while (i < code.size())
                {
                    uint32_t wc = code[i] >> 16;
                    uint32_t op = code[i] & 0xFFFF;
                    if (wc == 0)
                        break;
                    // OpFunction: [wc|54, resultType, funcId, control, funcType]
                    if (op == 54 && wc >= 3 && (i + 2) < code.size())
                    {
                        currentFunc = code[i + 2];
                        funcs[currentFunc];
                    }
                    // OpFunctionEnd: [wc|56]
                    else if (op == 56)
                        currentFunc = 0;
                    else if (currentFunc != 0)
                    {
                        // OpLoad: [wc|61, resultType, resultId, pointer]
                        if (op == 61 && wc >= 4 && (i + 3) < code.size())
                        {
                            if (code[i + 3] == samplerId)
                                funcs[currentFunc].loadsDepthSampler = true;
                        }
                        // OpFunctionCall: [wc|57, resultType, resultId, funcId, args...]
                        if (op == 57 && wc >= 4 && (i + 3) < code.size())
                            funcs[currentFunc].callees.push_back(code[i + 3]);
                    }
                    i += wc;
                }

                std::queue<uint32_t> worklist;
                std::set<uint32_t> visited;
                for (uint32_t ep : entryFuncIds)
                {
                    worklist.push(ep);
                    visited.insert(ep);
                }
                while (!worklist.empty())
                {
                    uint32_t fid = worklist.front();
                    worklist.pop();
                    auto it = funcs.find(fid);
                    if (it == funcs.end())
                        continue;
                    if (it->second.loadsDepthSampler)
                        return true;
                    for (uint32_t callee : it->second.callees)
                    {
                        if (visited.insert(callee).second)
                            worklist.push(callee);
                    }
                }
                return false;
            };

            for (const auto& samp : module.samplers)
            {
                if (samp.texture_name != depthTexName)
                    continue;
                if (isSamplerUsedInSpirv(samp.id))
                    return true;
            }
            return false;
        }

        struct MemoryCache
        {
            std::mutex mutex;
            std::list<std::string> order;
            std::unordered_map<std::string, std::pair<std::shared_ptr<const CompiledReshadeEffect>, std::list<std::string>::iterator>> map;

            std::shared_ptr<const CompiledReshadeEffect> get(const std::string& key)
            {
                std::lock_guard<std::mutex> lock(mutex);
                auto it = map.find(key);
                if (it == map.end())
                    return nullptr;
                order.erase(it->second.second);
                order.push_front(key);
                it->second.second = order.begin();
                return it->second.first;
            }

            void put(const std::string& key, std::shared_ptr<const CompiledReshadeEffect> value)
            {
                std::lock_guard<std::mutex> lock(mutex);
                auto it = map.find(key);
                if (it != map.end())
                {
                    order.erase(it->second.second);
                    map.erase(it);
                }
                order.push_front(key);
                map[key] = {std::move(value), order.begin()};
                while (map.size() > MEMORY_CACHE_CAP)
                {
                    map.erase(order.back());
                    order.pop_back();
                }
            }
        };

        MemoryCache memoryCache;

        // compileError set (not cached) when the failure is environmental, e.g. a
        // missing file; compile failures themselves are cacheable results.
        std::shared_ptr<CompiledReshadeEffect> compileEffect(
            const std::string& fxPath,
            const std::vector<std::pair<std::string, std::string>>& macroDefinitions,
            const std::vector<std::string>& includePaths,
            bool& cacheable)
        {
            auto e = std::make_shared<CompiledReshadeEffect>();
            cacheable = true;

            reshadefx::preprocessor preprocessor;
            addReshadeBaseMacros(preprocessor);
            for (const auto& [name, value] : macroDefinitions)
                preprocessor.add_macro_definition(name, value);
            for (const auto& path : includePaths)
                preprocessor.add_include_path(path);

            if (!preprocessor.append_file(fxPath))
            {
                e->error = "Failed to load shader file";
                std::string ppErrors = preprocessor.errors();
                if (!ppErrors.empty())
                    e->error += ": " + ppErrors;
                cacheable = false;
                return e;
            }

            auto recordIncludes = [&]() {
                for (const auto& path : preprocessor.included_files())
                {
                    std::string p = path.string();
                    std::string content;
                    if (readFileBytes(p, content))
                        e->includedFiles.push_back({p, contentHash(content)});
                }
            };

            // errors() mixes warnings in; only "preprocessor error:" is fatal.
            std::string ppErrors = preprocessor.errors();
            if (!ppErrors.empty() && ppErrors.find("preprocessor error:") != std::string::npos)
            {
                e->error = "Preprocessor errors: " + ppErrors;
                recordIncludes();
                return e;
            }

            e->usedMacros = preprocessor.used_macro_definitions();
            recordIncludes();

            reshadefx::parser parser;
            std::unique_ptr<reshadefx::codegen> codegen(reshadefx::create_codegen_spirv(
                true /* vulkan semantics */, true /* debug info */, true /* uniforms to spec constants */, true /* flip vertex shader */));

            if (!parser.parse(std::move(preprocessor.output()), codegen.get()))
            {
                std::string parseErr = parser.errors();
                std::string reason = reshadeUnsupportedFeature(parseErr);
                e->error = reason.empty() ? "Parse errors: " + parseErr : reason;
                return e;
            }

            std::string parseErrors = parser.errors();
            if (!parseErrors.empty())
                e->warnings = "Warnings: " + parseErrors;

            codegen->write_result(e->module);

            if (e->module.techniques.empty())
            {
                e->error = "shader has no techniques";
                return e;
            }
            if (e->module.spirv.empty())
            {
                e->error = "shader produced empty SPIR-V";
                return e;
            }

            e->usesDepth = moduleUsesDepth(e->module);
            return e;
        }
    } // anonymous namespace

    std::shared_ptr<const CompiledReshadeEffect> getOrCompileReshadeEffect(
        const std::string& fxPath,
        const std::vector<std::pair<std::string, std::string>>& macroDefinitions,
        const std::vector<std::string>& includePaths)
    {
        std::string fxContent;
        bool haveFx = readFileBytes(fxPath, fxContent);

        std::string key;
        if (haveFx)
        {
            std::string blob;
            blob += std::to_string(SCHEMA_VERSION);
            blob += '\x1f';
            for (const auto& [name, value] : macroDefinitions)
            {
                blob += name;
                blob += '\x1e';
                blob += value;
                blob += '\x1e';
            }
            blob += '\x1f';
            for (const auto& path : includePaths)
            {
                blob += path;
                blob += '\x1e';
            }
            blob += '\x1f';
            blob += fxPath;
            blob += '\x1f';
            blob += fxContent;
            key = hashName(blob);

            if (auto hit = memoryCache.get(key))
            {
                if (includedFilesUnchanged(*hit))
                    return hit;
            }

            std::string dir = cacheDir();
            if (!dir.empty())
            {
                std::string path = dir + "/" + key + ".vkbfx";
                std::string payload;
                if (readFileBytes(path, payload))
                {
                    try
                    {
                        auto entry = deserializeEntry(payload);
                        if (includedFilesUnchanged(*entry))
                        {
                            Logger::debug("shader cache hit: " + fxPath);
                            memoryCache.put(key, entry);
                            return entry;
                        }
                        std::remove(path.c_str());
                    }
                    catch (const std::exception& ex)
                    {
                        Logger::warn("shader cache: discarding unreadable entry for " + fxPath + " (" + ex.what() + ")");
                        std::remove(path.c_str());
                    }
                }
            }
        }

        bool cacheable = false;
        auto entry = compileEffect(fxPath, macroDefinitions, includePaths, cacheable);

        if (haveFx && cacheable)
        {
            memoryCache.put(key, entry);

            std::string dir = cacheDir();
            if (!dir.empty() && ensureCacheDir(dir))
            {
                if (writeEntryFile(dir, key, serializeEntry(*entry)))
                {
                    static std::once_flag pruneOnce;
                    std::call_once(pruneOnce, [&dir]() { pruneDiskCache(dir); });
                }
            }
        }

        return entry;
    }

} // namespace vkBasalt
