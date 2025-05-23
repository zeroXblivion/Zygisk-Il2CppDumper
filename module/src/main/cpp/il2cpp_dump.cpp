//
// Created by Perfare on 2020/7/4.
//

#include "il2cpp_dump.h"
#include <dlfcn.h>
#include <cstdlib>
#include <cstring>
#include <cinttypes>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <unistd.h>
#include "xdl.h"
#include "log.h"
#include "il2cpp-tabledefs.h"
#include "il2cpp-class.h"
#include <sys/stat.h>
#include <cstdio>
#include <errno.h>

#define DO_API(r, n, p) r (*n) p

#include "il2cpp-api-functions.h"

#undef DO_API

static std::string il2cpp_module_name_global = "libil2cpp.so"; 
static void *g_xdl_addr_cache = nullptr;

static uint64_t il2cpp_base = 0;

void init_il2cpp_api(void *handle) {
#define DO_API(r, n, p) {                      \
    n = (r (*) p)xdl_sym(handle, #n, nullptr); \
    if(!n) {                                   \
        LOGW("api not found %s", #n);          \
    }                                          \
}

#include "il2cpp-api-functions.h"

#undef DO_API
}

std::string get_method_modifier(uint32_t flags) {
    std::stringstream outPut;
    auto access = flags & METHOD_ATTRIBUTE_MEMBER_ACCESS_MASK;
    switch (access) {
        case METHOD_ATTRIBUTE_PRIVATE:
            outPut << "private ";
            break;
        case METHOD_ATTRIBUTE_PUBLIC:
            outPut << "public ";
            break;
        case METHOD_ATTRIBUTE_FAMILY:
            outPut << "protected ";
            break;
        case METHOD_ATTRIBUTE_ASSEM:
        case METHOD_ATTRIBUTE_FAM_AND_ASSEM:
            outPut << "internal ";
            break;
        case METHOD_ATTRIBUTE_FAM_OR_ASSEM:
            outPut << "protected internal ";
            break;
    }
    if (flags & METHOD_ATTRIBUTE_STATIC) {
        outPut << "static ";
    }
    if (flags & METHOD_ATTRIBUTE_ABSTRACT) {
        outPut << "abstract ";
        if ((flags & METHOD_ATTRIBUTE_VTABLE_LAYOUT_MASK) == METHOD_ATTRIBUTE_REUSE_SLOT) {
            outPut << "override ";
        }
    } else if (flags & METHOD_ATTRIBUTE_FINAL) {
        if ((flags & METHOD_ATTRIBUTE_VTABLE_LAYOUT_MASK) == METHOD_ATTRIBUTE_REUSE_SLOT) {
            outPut << "sealed override ";
        }
    } else if (flags & METHOD_ATTRIBUTE_VIRTUAL) {
        if ((flags & METHOD_ATTRIBUTE_VTABLE_LAYOUT_MASK) == METHOD_ATTRIBUTE_NEW_SLOT) {
            outPut << "virtual ";
        } else {
            outPut << "override ";
        }
    }
    if (flags & METHOD_ATTRIBUTE_PINVOKE_IMPL) {
        outPut << "extern ";
    }
    return outPut.str();
}

bool _il2cpp_type_is_byref(const Il2CppType *type) {
    auto byref = type->byref;
    if (il2cpp_type_is_byref) {
        byref = il2cpp_type_is_byref(type);
    }
    return byref;
}

std::string dump_method(Il2CppClass *klass) {
    std::stringstream outPut;
    outPut << "\n\t// Methods\n";
    void *iter = nullptr;
    while (auto method = il2cpp_class_get_methods(klass, &iter)) {
        uint64_t method_va = reinterpret_cast<uint64_t>(method->methodPointer);

        if (method->methodPointer) {
            xdl_info_t method_dlinfo;
            if (xdl_addr(reinterpret_cast<void*>(method->methodPointer), &method_dlinfo, &g_xdl_addr_cache) != 0) {
                const char* full_module_path = method_dlinfo.dli_fname;
                uint64_t module_base_for_method = reinterpret_cast<uint64_t>(method_dlinfo.dli_fbase);
                
                const char* short_module_name = "unknown_module.so";
                if (full_module_path) {
                    const char* found_slash = strrchr(full_module_path, '/');
                    if (found_slash) {
                        short_module_name = found_slash + 1;
                    } else {
                        short_module_name = full_module_path;
                    }
                }

                outPut << "\t// RVA: 0x";
                if (module_base_for_method != 0 && method_va >= module_base_for_method) {
                    outPut << std::hex << (method_va - module_base_for_method);
                } else {
                    outPut << std::hex << method_va; 
                    if (module_base_for_method != 0) {
                        LOGW("Method VA %p in %s is less than its module_base 0x%" PRIx64 ". RVA shown as VA.",
                             method->methodPointer, short_module_name, module_base_for_method);
                    }
                }
                outPut << " (Module: " << short_module_name << ") VA: 0x" << std::hex << method_va;
            } else {
                LOGW("xdl_addr failed for method VA %p. Using global il2cpp_base (for module %s) as fallback.",
                     method->methodPointer, il2cpp_module_name_global.c_str());
                outPut << "\t// RVA: 0x";
                if (il2cpp_base != 0 && method_va >= il2cpp_base) {
                     outPut << std::hex << (method_va - il2cpp_base);
                } else {
                    outPut << std::hex << method_va;
                     if (il2cpp_base != 0) {
                         LOGW("Method VA %p (xdl_addr_failed) is less than global il2cpp_base 0x%" PRIx64 ". RVA shown as VA.",
                              method->methodPointer, il2cpp_base);
                     }
                }
                outPut << " (module: " << il2cpp_module_name_global << " [fallback]) VA: 0x" << std::hex << method_va;
            }
        } else {
            outPut << "\t// RVA: 0x (module: n/a) VA: 0x0";
        }
        
        // Slot info (optional)
        // if (method->slot != 65535) {
        //    outPut << " Slot: " << std::dec << method->slot;
        // }
        outPut << "\n\t"; // Newline and tab for the method signature itself

        // Original signature dumping logic follows
        uint32_t iflags = 0;
        auto flags = il2cpp_method_get_flags(method, &iflags);
        outPut << get_method_modifier(flags);
        auto return_type = il2cpp_method_get_return_type(method);
        if (_il2cpp_type_is_byref(return_type)) {
            outPut << "ref ";
        }
        auto return_class = il2cpp_class_from_type(return_type);
        outPut << il2cpp_class_get_name(return_class) << " " << il2cpp_method_get_name(method)
               << "(";
        auto param_count = il2cpp_method_get_param_count(method);
        for (int i = 0; i < param_count; ++i) {
            auto param = il2cpp_method_get_param(method, i);
            auto attrs = param->attrs;
            if (_il2cpp_type_is_byref(param)) {
                if (attrs & PARAM_ATTRIBUTE_OUT && !(attrs & PARAM_ATTRIBUTE_IN)) {
                    outPut << "out ";
                } else if (attrs & PARAM_ATTRIBUTE_IN && !(attrs & PARAM_ATTRIBUTE_OUT)) {
                    outPut << "in ";
                } else {
                    outPut << "ref ";
                }
            } else {
                if (attrs & PARAM_ATTRIBUTE_IN) {
                    outPut << "[In] ";
                }
                if (attrs & PARAM_ATTRIBUTE_OUT) {
                    outPut << "[Out] ";
                }
            }
            auto parameter_class = il2cpp_class_from_type(param);
            outPut << il2cpp_class_get_name(parameter_class) << " "
                   << il2cpp_method_get_param_name(method, i);
            if (i < param_count - 1) {
                 outPut << ", ";
            }
        }
        outPut << ") { }\n";
    }
    return outPut.str();
}

std::string dump_property(Il2CppClass *klass) {
    std::stringstream outPut;
    outPut << "\n\t// Properties\n";
    void *iter = nullptr;
    while (auto prop_const = il2cpp_class_get_properties(klass, &iter)) {
        //TODO attribute
        auto prop = const_cast<PropertyInfo *>(prop_const);
        auto get = il2cpp_property_get_get_method(prop);
        auto set = il2cpp_property_get_set_method(prop);
        auto prop_name = il2cpp_property_get_name(prop);
        outPut << "\t";
        Il2CppClass *prop_class = nullptr;
        uint32_t iflags = 0;
        if (get) {
            outPut << get_method_modifier(il2cpp_method_get_flags(get, &iflags));
            prop_class = il2cpp_class_from_type(il2cpp_method_get_return_type(get));
        } else if (set) {
            outPut << get_method_modifier(il2cpp_method_get_flags(set, &iflags));
            auto param = il2cpp_method_get_param(set, 0);
            prop_class = il2cpp_class_from_type(param);
        }
        if (prop_class) {
            outPut << il2cpp_class_get_name(prop_class) << " " << prop_name << " { ";
            if (get) {
                outPut << "get; ";
            }
            if (set) {
                outPut << "set; ";
            }
            outPut << "}\n";
        } else {
            if (prop_name) {
                outPut << " // unknown property " << prop_name;
            }
        }
    }
    return outPut.str();
}

std::string dump_field(Il2CppClass *klass) {
    std::stringstream outPut;
    outPut << "\n\t// Fields\n";
    auto is_enum = il2cpp_class_is_enum(klass);
    void *iter = nullptr;
    while (auto field = il2cpp_class_get_fields(klass, &iter)) {
        //TODO attribute
        outPut << "\t";
        auto attrs = il2cpp_field_get_flags(field);
        auto access = attrs & FIELD_ATTRIBUTE_FIELD_ACCESS_MASK;
        switch (access) {
            case FIELD_ATTRIBUTE_PRIVATE:
                outPut << "private ";
                break;
            case FIELD_ATTRIBUTE_PUBLIC:
                outPut << "public ";
                break;
            case FIELD_ATTRIBUTE_FAMILY:
                outPut << "protected ";
                break;
            case FIELD_ATTRIBUTE_ASSEMBLY:
            case FIELD_ATTRIBUTE_FAM_AND_ASSEM:
                outPut << "internal ";
                break;
            case FIELD_ATTRIBUTE_FAM_OR_ASSEM:
                outPut << "protected internal ";
                break;
        }
        if (attrs & FIELD_ATTRIBUTE_LITERAL) {
            outPut << "const ";
        } else {
            if (attrs & FIELD_ATTRIBUTE_STATIC) {
                outPut << "static ";
            }
            if (attrs & FIELD_ATTRIBUTE_INIT_ONLY) {
                outPut << "readonly ";
            }
        }
        auto field_type = il2cpp_field_get_type(field);
        auto field_class = il2cpp_class_from_type(field_type);
        outPut << il2cpp_class_get_name(field_class) << " " << il2cpp_field_get_name(field);
        //TODO 获取构造函数初始化后的字段值
        if (attrs & FIELD_ATTRIBUTE_LITERAL && is_enum) {
            uint64_t val = 0;
            il2cpp_field_static_get_value(field, &val);
            outPut << " = " << std::dec << val;
        }
        outPut << "; // 0x" << std::hex << il2cpp_field_get_offset(field) << "\n";
    }
    return outPut.str();
}

std::string dump_type(const Il2CppType *type) {
    std::stringstream outPut;
    auto *klass = il2cpp_class_from_type(type);
    outPut << "\n// Namespace: " << il2cpp_class_get_namespace(klass) << "\n";
    auto flags = il2cpp_class_get_flags(klass);
    if (flags & TYPE_ATTRIBUTE_SERIALIZABLE) {
        outPut << "[Serializable]\n";
    }
    //TODO attribute
    auto is_valuetype = il2cpp_class_is_valuetype(klass);
    auto is_enum = il2cpp_class_is_enum(klass);
    auto visibility = flags & TYPE_ATTRIBUTE_VISIBILITY_MASK;
    switch (visibility) {
        case TYPE_ATTRIBUTE_PUBLIC:
        case TYPE_ATTRIBUTE_NESTED_PUBLIC:
            outPut << "public ";
            break;
        case TYPE_ATTRIBUTE_NOT_PUBLIC:
        case TYPE_ATTRIBUTE_NESTED_FAM_AND_ASSEM:
        case TYPE_ATTRIBUTE_NESTED_ASSEMBLY:
            outPut << "internal ";
            break;
        case TYPE_ATTRIBUTE_NESTED_PRIVATE:
            outPut << "private ";
            break;
        case TYPE_ATTRIBUTE_NESTED_FAMILY:
            outPut << "protected ";
            break;
        case TYPE_ATTRIBUTE_NESTED_FAM_OR_ASSEM:
            outPut << "protected internal ";
            break;
    }
    if (flags & TYPE_ATTRIBUTE_ABSTRACT && flags & TYPE_ATTRIBUTE_SEALED) {
        outPut << "static ";
    } else if (!(flags & TYPE_ATTRIBUTE_INTERFACE) && flags & TYPE_ATTRIBUTE_ABSTRACT) {
        outPut << "abstract ";
    } else if (!is_valuetype && !is_enum && flags & TYPE_ATTRIBUTE_SEALED) {
        outPut << "sealed ";
    }
    if (flags & TYPE_ATTRIBUTE_INTERFACE) {
        outPut << "interface ";
    } else if (is_enum) {
        outPut << "enum ";
    } else if (is_valuetype) {
        outPut << "struct ";
    } else {
        outPut << "class ";
    }
    outPut << il2cpp_class_get_name(klass); //TODO genericContainerIndex
    std::vector<std::string> extends;
    auto parent = il2cpp_class_get_parent(klass);
    if (!is_valuetype && !is_enum && parent) {
        auto parent_type = il2cpp_class_get_type(parent);
        if (parent_type->type != IL2CPP_TYPE_OBJECT) {
            extends.emplace_back(il2cpp_class_get_name(parent));
        }
    }
    void *iter = nullptr;
    while (auto itf = il2cpp_class_get_interfaces(klass, &iter)) {
        extends.emplace_back(il2cpp_class_get_name(itf));
    }
    if (!extends.empty()) {
        outPut << " : " << extends[0];
        for (int i = 1; i < extends.size(); ++i) {
            outPut << ", " << extends[i];
        }
    }
    outPut << "\n{";
    outPut << dump_field(klass);
    outPut << dump_property(klass);
    outPut << dump_method(klass);
    //TODO EventInfo
    outPut << "}\n";
    return outPut.str();
}

void il2cpp_api_init(void *handle) {
    LOGI("il2cpp_handle: %p", handle);
    init_il2cpp_api(handle); 

    if (!il2cpp_domain_get_assemblies) {
        LOGE("il2cpp_domain_get_assemblies is NULL after init_il2cpp_api. Cannot determine il2cpp_base. RVA values will be incorrect.");
        il2cpp_base = 0;
        il2cpp_module_name_global = "unknown_il2cpp_module.so";
        return;
    }

    LOGI("Memory address of resolved 'il2cpp_domain_get_assemblies' function: %p", (void*)il2cpp_domain_get_assemblies);

    xdl_info_t dlinfo;
    if (handle && xdl_info(handle, XDL_DI_DLINFO, &dlinfo) == 0) {
        il2cpp_base = reinterpret_cast<uint64_t>(dlinfo.dli_fbase);
        if (dlinfo.dli_fname) {
            const char* short_name = strrchr(dlinfo.dli_fname, '/');
            if (short_name) {
                il2cpp_module_name_global = short_name + 1;
            } else {
                il2cpp_module_name_global = dlinfo.dli_fname;
            }
        } else {
            il2cpp_module_name_global = "libil2cpp.so"; 
        }
        LOGI("Global il2cpp_base determined using xdl_info: 0x%" PRIx64 "", il2cpp_base);
        LOGI("Global Il2Cpp Module Name set to: %s (from path: %s)", il2cpp_module_name_global.c_str(), dlinfo.dli_fname ? dlinfo.dli_fname : "N/A");
        
        if (il2cpp_base == 0) {
            LOGE("xdl_info reported a base address of 0 for the main Il2Cpp module. RVA values might be incorrect.");
        } else if (reinterpret_cast<uint64_t>(il2cpp_domain_get_assemblies) < il2cpp_base) {
            LOGW("Warning: 'il2cpp_domain_get_assemblies' (%p) is lower than the determined il2cpp_base (0x%" PRIx64 ") for %s. This might indicate an issue.",
                 (void*)il2cpp_domain_get_assemblies, il2cpp_base, il2cpp_module_name_global.c_str());
        }

    } else {
        LOGE("xdl_info failed to get dli_fbase for libil2cpp.so or handle was null. Main RVA values will be incorrect.");
        il2cpp_base = 0; 
        il2cpp_module_name_global = "unknown_il2cpp_module.so";
    }

    if (il2cpp_base == 0 && strcmp(il2cpp_module_name_global.c_str(), "unknown_il2cpp_module.so") != 0) {
         LOGE("Failed to obtain a valid il2cpp_base for %s. RVA calculations may be incorrect.", il2cpp_module_name_global.c_str());
    }
}

struct IterateData {
    const char* output_dir_files;
    const char* app_package_name;
};

static int save_library_callback(struct dl_phdr_info *info, size_t size, void *data) {
    IterateData* iterate_data = (IterateData*)data;
    const char* lib_path = info->dlpi_name;

    if (!lib_path || lib_path[0] == '\0') {
        LOGD("Skipping due to null or empty library path.");
        return 0;
    }

    if (!strstr(lib_path, ".so")) {
        LOGD("Skipping non-.so file: %s", lib_path);
        return 0;
    }

    LOGI("Processing library path: %s", lib_path);

    bool should_copy = false;

    if (strstr(lib_path, iterate_data->app_package_name) != nullptr) {
        should_copy = true;
        LOGI("Decision: COPY (Path contains package name '%s') - Path: %s", iterate_data->app_package_name, lib_path);
    } else if (strstr(lib_path, "libil2cpp.so") || strstr(lib_path, "libcsharp.so") || strstr(lib_path, "libunity.so")) {
        should_copy = true;
        LOGI("Decision: COPY (Known game library name) - Path: %s", lib_path);
    } else if (strncmp(lib_path, "/system/", strlen("/system/")) == 0 ||
               strncmp(lib_path, "/apex/", strlen("/apex/")) == 0 ||
               strncmp(lib_path, "/vendor/", strlen("/vendor/")) == 0) {
        LOGI("Decision: SKIP (System/vendor path) - Path: %s", lib_path);
        should_copy = false;
    } else {
        should_copy = true;
        LOGI("Decision: COPY (Default for non-system .so) - Path: %s", lib_path);
    }
    
    if (!should_copy) {
        return 0;
    }

    const char *lib_name_ptr = strrchr(lib_path, '/');
    const char *lib_name = lib_name_ptr ? (lib_name_ptr + 1) : lib_path;

    if (lib_name[0] == '\0') {
        LOGW("Could not extract library filename from path: %s", lib_path);
        return 0;
    }

    std::string dest_path_str = std::string(iterate_data->output_dir_files) + "/" + lib_name;
    const char* dest_path = dest_path_str.c_str();

    struct stat st_lib_path;
    if (stat(lib_path, &st_lib_path) != 0 || !S_ISREG(st_lib_path.st_mode)) {
        LOGW("Source path is not a regular file or stat failed for '%s': %s", lib_path, strerror(errno));
        return 0;
    }

    struct stat st_dest_path;
    if (stat(dest_path, &st_dest_path) == 0 && S_ISREG(st_dest_path.st_mode)) {
        if (st_lib_path.st_size == st_dest_path.st_size) {
            LOGI("Library '%s' (from '%s') already exists at '%s' with the same size. Skipping.", lib_name, lib_path, dest_path);
            return 0;
        } else {
            LOGI("Library '%s' exists at destination '%s' but with different size (%ld vs %ld). Will overwrite.", lib_name, dest_path, (long)st_dest_path.st_size, (long)st_lib_path.st_size);
        }
    }

    std::ifstream src(lib_path, std::ios::binary);
    if (!src.is_open()) {
        LOGE("Failed to open source library '%s' for reading: %s", lib_path, strerror(errno));
        return 0;
    }

    std::ofstream dst(dest_path, std::ios::binary);
    if (!dst.is_open()) {
        LOGE("Failed to open destination path '%s' for writing: %s", dest_path, strerror(errno));
        src.close();
        return 0;
    }

    LOGI("Attempting to copy '%s' (%ld bytes) to '%s'", lib_path, (long)st_lib_path.st_size, dest_path);
    
    char buffer[4096];
    while (src.read(buffer, sizeof(buffer)) || src.gcount() > 0) {
        dst.write(buffer, src.gcount());
        if (dst.fail()) {
            break; 
        }
    }

    bool copy_failed = src.fail() && !src.eof() || dst.fail();
    
    src.close();
    dst.close();

    if (copy_failed) {
        LOGE("Error during copy of '%s' to '%s'. Src_eof: %d, Src_fail: %d, Dst_fail: %d. Attempting to remove partial file. Error: %s",
             lib_path, dest_path, src.eof(), src.fail(), dst.fail(), strerror(errno));
        if (remove(dest_path) != 0) {
            LOGE("Failed to remove partially written file '%s': %s", dest_path, strerror(errno));
        }
    } else {
        struct stat st_final_dest;
        if (stat(dest_path, &st_final_dest) == 0) {
            LOGI("Successfully copied '%s' to '%s' (%ld bytes written)", lib_name, dest_path, (long)st_final_dest.st_size);
        } else {
            LOGI("Successfully copied '%s' to '%s' (stat after copy failed)", lib_name, dest_path);
        }
    }
    return 0;
}

// Helper function to copy a single file
static bool copy_general_file(const char* src_path, const char* dst_path) {
    std::ifstream src(src_path, std::ios::binary);
    if (!src.is_open()) {
        LOGE("copy_general_file: Failed to open source file '%s': %s", src_path, strerror(errno));
        return false;
    }

    std::ofstream dst(dst_path, std::ios::binary);
    if (!dst.is_open()) {
        LOGE("copy_general_file: Failed to open destination file '%s': %s", dst_path, strerror(errno));
        src.close();
        return false;
    }

    LOGI("copy_general_file: Attempting to copy '%s' to '%s'", src_path, dst_path);

    char buffer[4096];
    while (src.read(buffer, sizeof(buffer)) || src.gcount() > 0) {
        dst.write(buffer, src.gcount());
        if (dst.fail()) {
            break;
        }
    }

    bool copy_failed = src.fail() && !src.eof() || dst.fail();

    src.close();
    dst.close();

    if (copy_failed) {
        LOGE("copy_general_file: Error during copy of '%s' to '%s'. Attempting to remove partial file. Error: %s",
             src_path, dst_path, strerror(errno));
        if (remove(dst_path) != 0) {
            LOGE("copy_general_file: Failed to remove partially written file '%s': %s", dst_path, strerror(errno));
        }
        return false;
    } else {
        struct stat st_final_dest;
        if (stat(dst_path, &st_final_dest) == 0) {
            LOGI("copy_general_file: Successfully copied '%s' to '%s' (%ld bytes written)", src_path, dst_path, (long)st_final_dest.st_size);
        } else {
            LOGI("copy_general_file: Successfully copied '%s' to '%s' (stat after copy failed for destination)", src_path, dst_path);
        }
        return true;
    }
}

// Function to find and save global-metadata.dat
static void save_global_metadata(const char* app_data_root_dir, const char* target_output_dir) {
    LOGI("Attempting to find and save global-metadata.dat...");
    LOGI("Using app data root: %s", app_data_root_dir);
    LOGI("Target output directory: %s", target_output_dir);

    const char* metadata_filename = "global-metadata.dat";
    std::vector<std::string> relative_search_paths;

    relative_search_paths.push_back("il2cpp/Metadata/" + std::string(metadata_filename));
    relative_search_paths.push_back("files/il2cpp/Metadata/" + std::string(metadata_filename));
    relative_search_paths.push_back("il2cpp_data/Metadata/" + std::string(metadata_filename));
    relative_search_paths.push_back("files/il2cpp_data/Metadata/" + std::string(metadata_filename));
    relative_search_paths.push_back("files/" + std::string(metadata_filename));
    relative_search_paths.push_back(std::string(metadata_filename));


    bool found_and_copied = false;
    for (const auto& rel_path : relative_search_paths) {
        std::string full_src_path = std::string(app_data_root_dir) + "/" + rel_path;
        LOGI("Checking for metadata at: %s", full_src_path.c_str());

        struct stat st_src;
        if (stat(full_src_path.c_str(), &st_src) == 0 && S_ISREG(st_src.st_mode)) {
            LOGI("Found potential global-metadata.dat at: %s (%ld bytes)", full_src_path.c_str(), (long)st_src.st_size);
            std::string dest_file_path = std::string(target_output_dir) + "/" + metadata_filename;
            
            if (copy_general_file(full_src_path.c_str(), dest_file_path.c_str())) {
                LOGI("Successfully saved global-metadata.dat from %s to %s", full_src_path.c_str(), dest_file_path.c_str());
                found_and_copied = true;
                break;
            } else {
                LOGE("Failed to copy global-metadata.dat from %s. Will try other paths.", full_src_path.c_str());
            }
        }
    }

    if (!found_and_copied) {
        LOGW("global-metadata.dat was not found in any of the common searched locations or could not be copied.");
    }
}

void il2cpp_dump(const char *outDir) {
    g_xdl_addr_cache = nullptr; 

    LOGI("dumping...");
    size_t assembly_size;
    auto domain = il2cpp_domain_get();
    auto assemblies = il2cpp_domain_get_assemblies(domain, &assembly_size);
    std::stringstream imageOutput;
    for (size_t i = 0; i < assembly_size; ++i) { 
        auto image = il2cpp_assembly_get_image(assemblies[i]);
        imageOutput << "// Image " << i << ": " << il2cpp_image_get_name(image) << "\n";
    }
    std::vector<std::string> outPuts;
    if (il2cpp_image_get_class) {
        LOGI("Version greater than 2018.3");
        for (size_t i = 0; i < assembly_size; ++i) { 
            auto image = il2cpp_assembly_get_image(assemblies[i]);
            std::stringstream imageStr;
            imageStr << "\n// Dll : " << il2cpp_image_get_name(image);
            auto classCount = il2cpp_image_get_class_count(image);
            for (size_t j = 0; j < classCount; ++j) { 
                auto klass = il2cpp_image_get_class(image, j);
                auto type = il2cpp_class_get_type(const_cast<Il2CppClass *>(klass));
                auto outPut = imageStr.str() + dump_type(type); // dump_type calls dump_method
                outPuts.push_back(outPut);
            }
        }
    } else {
        LOGI("Version less than 2018.3");
        auto corlib = il2cpp_get_corlib();
        auto assemblyClass = il2cpp_class_from_name(corlib, "System.Reflection", "Assembly");
        auto assemblyLoad = il2cpp_class_get_method_from_name(assemblyClass, "Load", 1);
        auto assemblyGetTypes = il2cpp_class_get_method_from_name(assemblyClass, "GetTypes", 0);
        if (!assemblyLoad || !assemblyLoad->methodPointer) {
            LOGE("miss Assembly::Load");
        }
        if (!assemblyGetTypes || !assemblyGetTypes->methodPointer) {
            LOGE("miss Assembly::GetTypes");
        }

        if (assemblyLoad && assemblyLoad->methodPointer && assemblyGetTypes && assemblyGetTypes->methodPointer) {
            typedef void *(*Assembly_Load_ftn)(void *, Il2CppString *, void *);
            typedef Il2CppArray *(*Assembly_GetTypes_ftn)(void *, void *);
            for (size_t i = 0; i < assembly_size; ++i) {
                auto image = il2cpp_assembly_get_image(assemblies[i]);
                std::stringstream imageStr;
                auto image_name = il2cpp_image_get_name(image);
                imageStr << "\n// Dll : " << image_name;
                auto imageName = std::string(image_name);
                auto pos = imageName.rfind('.');
                auto imageNameNoExt = imageName.substr(0, pos);
                auto assemblyFileName = il2cpp_string_new(imageNameNoExt.data());
                auto reflectionAssembly = ((Assembly_Load_ftn) assemblyLoad->methodPointer)(nullptr,
                                                                                            assemblyFileName,
                                                                                            nullptr);
                if (reflectionAssembly) {
                    auto reflectionTypes = ((Assembly_GetTypes_ftn) assemblyGetTypes->methodPointer)(
                        reflectionAssembly, nullptr);
                    if (reflectionTypes) {
                        auto items = reflectionTypes->vector;
                        for (size_t j = 0; j < reflectionTypes->max_length; ++j) { 
                            auto klass = il2cpp_class_from_system_type((Il2CppReflectionType *) items[j]);
                            if (klass) { // Add null check for klass
                                auto type = il2cpp_class_get_type(klass);
                                auto outPut = imageStr.str() + dump_type(type); 
                                outPuts.push_back(outPut);
                            } else {
                                LOGW("il2cpp_class_from_system_type returned null for item %zu in image %s", j, image_name);
                            }
                        }
                    } else {
                        LOGW("Assembly::GetTypes returned null for image: %s", image_name);
                    }
                } else {
                     LOGW("Assembly::Load returned null for image name: %s", imageNameNoExt.c_str());
                }
            }
        } else {
            LOGE("Cannot use reflection for type dumping due to missing Assembly methods. Dump.cs may be incomplete.");
        }
    }

    LOGI("write dump file");
    auto outPath = std::string(outDir).append("/files/dump.cs");
    std::ofstream outStream(outPath);
    if (!outStream.is_open()) {
        LOGE("Failed to open dump.cs for writing at %s: %s", outPath.c_str(), strerror(errno));
    } else {
        outStream << imageOutput.str();
        auto count = outPuts.size();
        for (size_t i = 0; i < count; ++i) { // Use size_t
            outStream << outPuts[i];
        }
        outStream.close();
        if (outStream.fail()) {
             LOGE("Failed during writing or closing dump.cs at %s", outPath.c_str());
        } else {
            LOGI("dump.cs written successfully to %s", outPath.c_str());
        }
    }

    // --- Save Libraries ---
    LOGI("Attempting to save used native libraries...");
    std::string files_output_dir = std::string(outDir) + "/files";
    std::string current_package_name_str;
    const char* data_data_prefix = "/data/data/";
    const char* data_user_prefix = "/data/user/0/";

    if (strncmp(outDir, data_user_prefix, strlen(data_user_prefix)) == 0) {
        current_package_name_str = std::string(outDir + strlen(data_user_prefix));
    } else if (strncmp(outDir, data_data_prefix, strlen(data_data_prefix)) == 0) {
        current_package_name_str = std::string(outDir + strlen(data_data_prefix));
    } else {
        LOGW("outDir format '%s' unexpected. Cannot reliably extract package name.", outDir);
    }

    if (current_package_name_str.empty()) {
         LOGE("Could not determine package name from outDir ('%s'). .SO library filtering might be less effective.", outDir);
         current_package_name_str = "unknown.package.name.fallback";
    }
    
    LOGI("Using package name for filtering: %s", current_package_name_str.c_str());
    LOGI("Output directory for libraries: %s", files_output_dir.c_str());
    
    struct stat st_dir_check;
    bool dir_ok = false;
    if (stat(files_output_dir.c_str(), &st_dir_check) == -1) {
        if (mkdir(files_output_dir.c_str(), 0755) == 0) {
            LOGI("Output directory %s created.", files_output_dir.c_str());
            dir_ok = true;
        } else {
            LOGE("Failed to create output directory %s: %s.", files_output_dir.c_str(), strerror(errno));
        }
    } else if (S_ISDIR(st_dir_check.st_mode)) {
        LOGI("Output directory %s already exists.", files_output_dir.c_str());
        dir_ok = true;
    } else {
        LOGE("Path %s exists but is not a directory.", files_output_dir.c_str());
    }
    
    if (dir_ok) {
        IterateData iter_data;
        iter_data.output_dir_files = files_output_dir.c_str();
        iter_data.app_package_name = current_package_name_str.c_str();
        int iteration_result = xdl_iterate_phdr(save_library_callback, &iter_data, XDL_FULL_PATHNAME);
        if (iteration_result != 0) {
            LOGW("xdl_iterate_phdr finished with a non-zero status: %d", iteration_result);
        }
    } else {
        LOGE("Skipping native library saving due to output directory issue.");
    }
    LOGI("Finished attempting to save native libraries.");

    // --- Save Metadata ---
    if (dir_ok) { // Only try to save if output dir is ok
        save_global_metadata(outDir, files_output_dir.c_str());
    } else {
         LOGE("Skipping global-metadata saving due to output directory issue.");
    }

    // --- Clean up Cache ---
    LOGI("Cleaning up xdl_addr cache.");
    xdl_addr_clean(&g_xdl_addr_cache); 

    LOGI("dump done!");
}
