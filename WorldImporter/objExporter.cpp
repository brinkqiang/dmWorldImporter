#include "objExporter.h"
#include <chrono>
#include <fstream>
#include <sstream>

using namespace std::chrono;

// 辅助函数：计算数值转换为字符串后的长度
template <typename T>
int calculateStringLength(T value, int precision = 6) {
    char buffer[64];
    return snprintf(buffer, sizeof(buffer), "%.*f", precision, static_cast<double>(value));
}

// 快速计算整数的字符串长度（正数）
inline int calculateIntLength(int value) {
    if (value == 0) return 1;
    int length = 0;
    bool is_negative = value < 0;
    value = abs(value); // 取绝对值
    while (value != 0) {
        value /= 10;
        length++;
    }
    if (is_negative) {
        length++; // 负数需要额外的符号位
    }
    return length;
}
// 快速计算浮点数转换为 "%.6f" 格式后的字符串长度（数学估算）
inline int calculateFloatStringLength(float value) {
    if (value == floor(value)) {  // 整数
        return calculateIntLength(static_cast<int>(value));
    }
    else {
        const bool negative = value < 0.0f;
        const double absValue = std::abs(static_cast<double>(value));

        // 处理特殊情况：0.0
        if (absValue < 1e-7) {
            return negative ? 9 : 8; // "-0.000000" 或 "0.000000"
        }

        // 计算整数部分位数
        int integerDigits;
        if (absValue < 1.0) {
            integerDigits = 1; // 例如 0.123456 -> "0.123456"
        }
        else {
            integerDigits = static_cast<int>(std::floor(std::log10(absValue))) + 1;
        }

        // 总长度 = 符号位 + 整数部分 + 小数点 + 6位小数
        return (negative ? 1 : 0) + integerDigits + 1 + 6;
    }

}
// 快速整数转字符串（正数版）
inline char* fast_itoa_positive(uint32_t value, char* ptr) {
    char* start = ptr;
    do {
        *ptr++ = '0' + (value % 10);
        value /= 10;
    } while (value > 0);
    std::reverse(start, ptr);
    return ptr;
}

// 快速整数转字符串（带符号）
inline char* fast_itoa(int value, char* ptr) {
    if (value == 0) {
        *ptr++ = '0';
        return ptr;
    }

    bool negative = value < 0;
    if (negative) {
        *ptr++ = '-';
        value = -value;
    }

    // 调用无符号整数的辅助函数
    ptr = fast_itoa_positive(static_cast<uint32_t>(value), ptr);

    return ptr;
}

// 快速浮点转字符串（固定6位小数）
inline char* fast_ftoa(float value, char* ptr) {
    if (value == floor(value)) {  // 检查是否是整数
        return fast_itoa(static_cast<int>(value), ptr);
    }
    else {
        // 原有浮点数处理逻辑
        const int64_t scale = 1000000;
        bool negative = value < 0;
        if (negative) {
            *ptr++ = '-';
            value = -value;
        }

        if (std::isinf(value)) {
            memcpy(ptr, "inf", 3);
            return ptr + 3;
        }

        int64_t scaled = static_cast<int64_t>(std::round(value * scale));
        int64_t integer_part = scaled / scale;
        int64_t fractional_part = scaled % scale;

        ptr = fast_itoa(static_cast<int>(integer_part), ptr);
        *ptr++ = '.';

        for (int i = 5; i >= 0; --i) {
            ptr[i] = '0' + (fractional_part % 10);
            fractional_part /= 10;
        }
        ptr += 6;

        return ptr;
    }
}

//——————————————导出.obj/.mtl方法—————————————

// 主函数：通过内存映射高效导出.obj文件
void createObjFileViaMemoryMapped(const ModelData& data, const std::string& objName) {
    std::string exeDir = getExecutableDir();
    std::string objFilePath = exeDir + objName + ".obj";
    std::string mtlFilePath = objName + ".mtl";

    //--- 步骤1：优化后的总大小计算 ---
    size_t totalSize = 0;
    // 文件头部分
    totalSize += snprintf(nullptr, 0, "mtllib %s\n", mtlFilePath.c_str());
    std::string modelName = objName.substr(objName.find_last_of("//") + 1);
    totalSize += snprintf(nullptr, 0, "o %s\n\n", modelName.c_str());


    // 预计算所有浮点数的字符串长度
    std::vector<int> vertexLengths(data.vertices.size());
    for (size_t i = 0; i < data.vertices.size(); ++i) {
        vertexLengths[i] = calculateFloatStringLength(data.vertices[i]);
    }

    // 顶点数据大小计算（并行优化）
    const size_t vertexCount = data.vertices.size() / 3;
#pragma omp parallel for reduction(+:totalSize)
    for (size_t i = 0; i < vertexCount; ++i) {
        const size_t base = i * 3;
        const int lenX = vertexLengths[base];
        const int lenY = vertexLengths[base + 1];
        const int lenZ = vertexLengths[base + 2];
        totalSize += 2 + lenX + 1 + lenY + 1 + lenZ + 1; // "v " x y z "\n"
    }
    // UV数据大小计算（同理优化）
    std::vector<int> uvLengths(data.uvCoordinates.size());
    for (size_t i = 0; i < data.uvCoordinates.size(); ++i) {
        uvLengths[i] = calculateFloatStringLength(data.uvCoordinates[i]);
    }

    const size_t uvCount = data.uvCoordinates.size() / 2;
#pragma omp parallel for reduction(+:totalSize)
    for (size_t i = 0; i < uvCount; ++i) {
        const size_t base = i * 2;
        const int lenU = uvLengths[base];
        const int lenV = uvLengths[base + 1];
        totalSize += 3 + lenU + 1 + lenV + 1; // "vt " u v "\n"
    }


    // 面数据分组计算
    std::vector<std::vector<size_t>> materialGroups(data.materialNames.size());
    const size_t totalFaces = data.faces.size() / 4;
    for (size_t faceIdx = 0; faceIdx < totalFaces; ++faceIdx) {
        const int matIndex = data.materialIndices[faceIdx];
        if (matIndex != -1 && matIndex < materialGroups.size()) {
            materialGroups[matIndex].push_back(faceIdx);
        }
    }


    // 预计算材质名称的usemtl行长度
    std::vector<size_t> usemtlLengths(data.materialNames.size());
#pragma omp parallel for
    for (int matIndex = 0; matIndex < data.materialNames.size(); ++matIndex) {
        usemtlLengths[matIndex] = 8 + data.materialNames[matIndex].size() + 1; // "usemtl " + name + "\n"
    }

    // 并行处理材质组
#pragma omp parallel for reduction(+:totalSize)
    for (int matIndex = 0; matIndex < materialGroups.size(); ++matIndex) {
        const auto& faces = materialGroups[matIndex];
        if (faces.empty()) continue;

        size_t localSize = usemtlLengths[matIndex];

        for (const size_t faceIdx : faces) {
            // 计算每个面的基础长度："f " + 4个顶点 + 换行
            size_t faceLength = 3; // "f " + '\n'

            // 预取顶点和UV索引
            const int* faceV = &data.faces[faceIdx * 4];
            const int* faceUV = &data.uvFaces[faceIdx * 4];

            // 展开循环处理4个顶点
            for (int i = 0; i < 4; ++i) {
                const int vIdx = faceV[i] + 1;
                const int uvIdx = faceUV[i] + 1;

                // 累加每个顶点的长度：v/uv + 空格
                faceLength += calculateIntLength(vIdx) +
                    calculateIntLength(uvIdx) + 2; // +2 对应 '/' 和空格
            }
            localSize += faceLength;
        }
        totalSize += localSize;
    }

    //--- 步骤2：分配内存缓冲区 ---
    std::vector<char> buffer(totalSize + 1); // +1为安全冗余
    char* ptr = buffer.data();
    //--- 步骤3：优化后的缓冲区填充 ---
    // 文件头（保持原有逻辑）
    ptr += sprintf_s(ptr, buffer.size() - (ptr - buffer.data()), "mtllib %s\n", mtlFilePath.c_str());
    ptr += sprintf_s(ptr, buffer.size() - (ptr - buffer.data()), "o %s\n\n", modelName.c_str());

    // 顶点数据（优化后）
    ptr += sprintf_s(ptr, buffer.size() - (ptr - buffer.data()), "# Vertices (%zu)\n", data.vertices.size() / 3);
    for (size_t i = 0; i < data.vertices.size(); i += 3) {
        memcpy(ptr, "v ", 2);
        ptr += 2;
        ptr = fast_ftoa(data.vertices[i], ptr);
        *ptr++ = ' ';
        ptr = fast_ftoa(data.vertices[i + 1], ptr);
        *ptr++ = ' ';
        ptr = fast_ftoa(data.vertices[i + 2], ptr);
        *ptr++ = '\n';
    }

    // UV数据（优化后）
    ptr += sprintf_s(ptr, buffer.size() - (ptr - buffer.data()), "\n# UVs (%zu)\n", data.uvCoordinates.size() / 2);
    for (size_t i = 0; i < data.uvCoordinates.size(); i += 2) {
        memcpy(ptr, "vt ", 3);
        ptr += 3;
        ptr = fast_ftoa(data.uvCoordinates[i], ptr);
        *ptr++ = ' ';
        ptr = fast_ftoa(data.uvCoordinates[i + 1], ptr);
        *ptr++ = '\n';
    }

    // 面数据（优化后）
    ptr += sprintf_s(ptr, buffer.size() - (ptr - buffer.data()), "\n# Faces (%zu)\n", totalFaces);
    for (size_t matIndex = 0; matIndex < materialGroups.size(); ++matIndex) {
        const auto& faces = materialGroups[matIndex];
        if (faces.empty()) continue;

        ptr += sprintf_s(ptr, buffer.size() - (ptr - buffer.data()), "usemtl %s\n", data.materialNames[matIndex].c_str());
        for (const size_t faceIdx : faces) {
            memcpy(ptr, "f ", 2);
            ptr += 2;
            for (int i = 0; i < 4; ++i) {
                const int vIdx = data.faces[faceIdx * 4 + i] + 1;
                const int uvIdx = data.uvFaces[faceIdx * 4 + i] + 1;

                // 检查是否为负数
                if (vIdx <= 0 || uvIdx <= 0) {
                    throw std::invalid_argument("Invalid vertex or UV index");
                }

                ptr = fast_itoa(vIdx, ptr);
                *ptr++ = '/';
                ptr = fast_itoa(uvIdx, ptr);
                *ptr++ = ' ';
            }
            *ptr++ = '\n';
        }
    }

    //--- 步骤4：内存映射写入 ---
    HANDLE hFile = CreateFileA(
        objFilePath.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        throw std::system_error(GetLastError(), std::system_category(), "CreateFile failed");
    }

    // 设置文件大小
    LARGE_INTEGER fileSize;
    fileSize.QuadPart = totalSize;
    if (!SetFilePointerEx(hFile, fileSize, nullptr, FILE_BEGIN) || !SetEndOfFile(hFile)) {
        CloseHandle(hFile);
        throw std::system_error(GetLastError(), std::system_category(), "SetFileSize failed");
    }

    // 创建内存映射
    HANDLE hMapping = CreateFileMapping(
        hFile,
        nullptr,
        PAGE_READWRITE,
        fileSize.HighPart,
        fileSize.LowPart,
        nullptr
    );

    if (!hMapping) {
        CloseHandle(hFile);
        throw std::system_error(GetLastError(), std::system_category(), "CreateFileMapping failed");
    }

    // 映射视图
    char* mappedData = static_cast<char*>(MapViewOfFile(
        hMapping,
        FILE_MAP_WRITE,
        0,
        0,
        totalSize
    ));

    if (!mappedData) {
        CloseHandle(hMapping);
        CloseHandle(hFile);
        throw std::system_error(GetLastError(), std::system_category(), "MapViewOfFile failed");
    }

    // 拷贝数据
    memcpy(mappedData, buffer.data(), totalSize);

    // 清理资源
    UnmapViewOfFile(mappedData);
    CloseHandle(hMapping);
    CloseHandle(hFile);
}

// 创建 .obj 文件并写入内容
void createObjFile(const ModelData& data, const std::string& objName) {
    std::string exeDir = getExecutableDir();

    std::string objFilePath = exeDir + objName + ".obj";
    std::string mtlFilePath = objName + ".mtl";

    // 提取模型名称
    std::string name;
    size_t commentPos = objName.find("//");
    if (commentPos != std::string::npos) {
        name = objName.substr(commentPos + 2);
    }

    // 使用流缓冲区进行拼接，减少IO操作次数
    std::ostringstream oss;

    // 写入文件头
    oss << "mtllib " << mtlFilePath << "\n";
    oss << "o " << name << "\n\n";
    // 写入顶点数据（每3个元素一个顶点）
    oss << "# Vertices (" << data.vertices.size() / 3 << ")\n";
    for (size_t i = 0; i < data.vertices.size(); i += 3) {
        oss << "v " << data.vertices[i] << " "
            << data.vertices[i + 1] << " "
            << data.vertices[i + 2] << "\n";
    }
    oss << "\n";

    // 写入UV坐标（每2个元素一个UV）
    oss << "# UVs (" << data.uvCoordinates.size() / 2 << ")\n";
    for (size_t i = 0; i < data.uvCoordinates.size(); i += 2) {
        oss << "vt " << data.uvCoordinates[i] << " "
            << data.uvCoordinates[i + 1] << "\n";
    }
    oss << "\n";

    // 按材质分组面（优化分组算法）
    std::vector<std::vector<size_t>> materialGroups(data.materialNames.size());
    const size_t totalFaces = data.faces.size() / 4;
    for (size_t faceIdx = 0; faceIdx < totalFaces; ++faceIdx) {
        const int matIndex = data.materialIndices[faceIdx];
        if (matIndex != -1 && matIndex < materialGroups.size()) {
            materialGroups[matIndex].push_back(faceIdx);
        }
    }

    // 写入面数据（优化内存访问模式）
    oss << "# Faces (" << totalFaces << ")\n";
    for (size_t matIndex = 0; matIndex < materialGroups.size(); ++matIndex) {
        const auto& faces = materialGroups[matIndex];
        if (faces.empty()) continue;

        oss << "usemtl " << data.materialNames[matIndex] << "\n";
        for (const size_t faceIdx : faces) {
            size_t base = faceIdx * 4;
            oss << "f ";
            for (int i = 0; i < 4; ++i) {
                const int vIdx = data.faces[base + i] + 1;
                const int uvIdx = data.uvFaces[base + i] + 1;
                oss << vIdx << "/" << uvIdx << " ";
            }
            oss << "\n";
        }
    }

    // 将缓冲区写入文件
    std::ofstream objFile(objFilePath, std::ios::binary); // 使用二进制模式提高写入速度
    if (objFile.is_open()) {
        objFile << oss.str();
        objFile.close();
    }
    else {
        std::cerr << "Error: Failed to create " << objFilePath << "\n";
    }
}
// 创建 .mtl 文件，接收 textureToPath 作为参数
void createMtlFile(const ModelData& data, const std::string& mtlFileName) {
    std::string exeDir = getExecutableDir();
    std::string fullMtlPath = exeDir + mtlFileName + ".mtl";

    std::ofstream mtlFile(fullMtlPath);
    if (mtlFile.is_open()) {
        for (size_t i = 0; i < data.materialNames.size(); ++i) {
            const std::string& textureName = data.materialNames[i];
            std::string texturePath = data.texturePaths[i];

            // 处理材质名称为 LIGHT 的情况
            if (texturePath== "None") {

                // 写入材质名称
                mtlFile << "newmtl " << textureName << "\n";

                // 设置更高的光泽度和亮度属性
                mtlFile << "Ns 200.000000\n"; // 高光泽度
                mtlFile << "Ka 1.000000 1.000000 1.000000\n"; // 高环境光
                mtlFile << "Ks 0.900000 0.900000 0.900000\n"; // 高镜面反射
                mtlFile << "Ke 0.900000 0.900000 0.900000\n"; // 高自发光
                mtlFile << "Ni 1.500000\n"; // 折射率
                mtlFile << "illum 2\n"; // 使用高反射光照模型

            }
            else {
                // 处理其他材质
                // 写入材质名称
                mtlFile << "newmtl " << textureName << "\n";

                // 保持原有固定材质属性
                mtlFile << "Ns 90.000000\n"; // 光泽度
                mtlFile << "Ka 1.000000 1.000000 1.000000\n"; // 环境光颜色
                mtlFile << "Ks 0.000000 0.000000 0.000000\n"; // 镜面反射
                mtlFile << "Ke 0.000000 0.000000 0.000000\n"; // 自发光
                mtlFile << "Ni 1.500000\n"; // 折射率
                mtlFile << "illum 1\n"; // 照明模型

                // 写入纹理信息，注意这里是相对路径
                if (mtlFileName.find("//") != std::string::npos) {
                    // 在 texturePath 前面添加 "../"
                    texturePath = "../" + texturePath;
                }

                // 确保路径以 ".png" 结尾，如果没有则加上
                if (texturePath.find(".png") == std::string::npos) {
                    texturePath += ".png";
                }

                mtlFile << "map_Kd " << texturePath << "\n"; // 颜色纹理
                mtlFile << "map_d " << texturePath << "\n"; // 透明度纹理
            }

            mtlFile << "\n"; // 添加空行，以便分隔不同材质
        }

        mtlFile.close();
    }
    else {
        std::cerr << "Failed to create .mtl file: " << mtlFileName << std::endl;
    }
}

// 单独的文件创建方法
void CreateModelFiles(const ModelData& data, const std::string& filename) {
    auto start = high_resolution_clock::now();  // 新增：开始时间点
    // 创建OBJ文件
    createObjFileViaMemoryMapped(data, filename);

    auto end = high_resolution_clock::now();  // 新增：结束时间点
    auto duration = duration_cast<milliseconds>(end - start);  // 新增：计算时间差
    std::cout << "模型导出obj耗时: " << duration.count() << " ms" << std::endl;  // 新增：输出到控制台
    // 创建MTL文件
    createMtlFile(data, filename);
}