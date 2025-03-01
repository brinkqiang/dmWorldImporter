#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#include "include/stb_image_write.h"
#include "include/stb_image.h"
#include "biome.h"
#include "block.h"
#include "GlobalCache.h"
#include "coord_conversion.h"
#include <iostream>
#include <stdexcept>
#include <fstream>
#include <string>

namespace {
    std::pair<std::string, std::string> splitBiomeName(const std::string& fullName) {
        const size_t colonPos = fullName.find(':');
        if (colonPos == std::string::npos)
            throw std::invalid_argument("Invalid biome format: " + fullName);
        return { fullName.substr(0, colonPos), fullName.substr(colonPos + 1) };
    }
}

// 初始化静态成员
std::unordered_map<std::string, BiomeInfo> Biome::biomeRegistry;
std::shared_mutex Biome::registryMutex;

// 自定义哈希函数，用于std::pair<int, int>
struct pair_hash {
    template <class T1, class T2>
    std::size_t operator ()(const std::pair<T1, T2>& p) const {
        auto h1 = std::hash<T1>{}(p.first);
        auto h2 = std::hash<T2>{}(p.second);
        return h1 ^ (h2 << 1);
    }
};

// 自定义哈希函数，用于std::pair<int, int, int>
struct triple_hash {
    template <class T1, class T2, class T3>
    std::size_t operator ()(const std::tuple<T1, T2, T3>& t) const {
        auto h1 = std::hash<T1>{}(std::get<0>(t));
        auto h2 = std::hash<T2>{}(std::get<1>(t));
        auto h3 = std::hash<T3>{}(std::get<2>(t));
        return h1 ^ (h2 << 1) ^ (h3 << 2);
    }
};

bool SaveColormapToFile(const std::vector<unsigned char>& pixelData,
    const std::string& namespaceName,
    const std::string& colormapName,
    std::string& savePath) {
    // 检查是否找到了纹理数据
    if (!pixelData.empty()) {

        // 获取当前工作目录（即 exe 所在的目录）
        char buffer[MAX_PATH];
        GetModuleFileNameA(NULL, buffer, MAX_PATH);
        std::string exePath = std::string(buffer);

        // 获取 exe 所在目录
        size_t pos = exePath.find_last_of("\\/");
        std::string exeDir = exePath.substr(0, pos);

        // 如果传入了 savePath, 则使用 savePath 作为保存目录，否则默认使用当前 exe 目录
        if (savePath.empty()) {
            savePath = exeDir + "\\colormap";  // 默认保存到 exe 目录下的 textures 文件夹
        }
        else {
            savePath = exeDir + "\\" + savePath;  // 使用提供的 savePath 路径
        }

        // 创建保存目录（如果不存在）
        if (GetFileAttributesA(savePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            // 文件夹不存在，创建它
            if (!CreateDirectoryA(savePath.c_str(), NULL)) {
                std::cerr << "Failed to create directory: " << savePath << std::endl;
                return false;
            }
        }

        // 处理 blockId，去掉路径部分，保留最后的文件名
        size_t lastSlashPos = colormapName.find_last_of("/\\");
        std::string fileName = (lastSlashPos == std::string::npos) ? colormapName : colormapName.substr(lastSlashPos + 1);

        // 构建保存路径，使用处理后的 blockId 作为文件名
        std::string filePath = savePath + "\\" + fileName + ".png";
        std::ofstream outputFile(filePath, std::ios::binary);

        //返回savePath，作为value
        savePath = filePath;

        if (outputFile.is_open()) {
            outputFile.write(reinterpret_cast<const char*>(pixelData.data()), pixelData.size());
            //std::cout << "Texture saved as '" << filePath << "'" << std::endl;
        }
        else {
            std::cerr << "Failed to open output file!" << std::endl;
            return false;
        }
    }
    else {
        std::cerr << "Failed to retrieve texture for " << colormapName << std::endl;
        return false;
    }
}


int GetBiomeId(int blockX, int blockY, int blockZ) {
    // 将世界坐标转换为区块坐标
    int chunkX, chunkZ;
    blockToChunk(blockX, blockZ, chunkX, chunkZ);

    // 将方块的Y坐标转换为子区块索引
    int sectionY;
    blockYToSectionY(blockY, sectionY);
    int adjustedSectionY = AdjustSectionY(sectionY);

    // 创建缓存键
    auto blockKey = std::make_tuple(chunkX, chunkZ, adjustedSectionY);

    // 检查 SectionCache 中是否存在对应的区块数据
    if (sectionCache.find(blockKey) == sectionCache.end()) {
        LoadAndCacheBlockData(chunkX, chunkZ);
    }

    const auto& biomeData = sectionCache[blockKey].biomeData;

    int biomeX = mod16(blockX) / 4;
    int biomeY = mod16(blockY) / 4;
    int biomeZ = mod16(blockZ) / 4;

    // 计算编码索引（16y + 4z + x）
    int index = 16 * biomeY + 4 * biomeZ + biomeX;

    // 获取并返回群系ID
    return (index < biomeData.size()) ? biomeData[index] : 0;
}


nlohmann::json Biome::GetBiomeJson(const std::string& namespaceName, const std::string& biomeId) {
    std::string cacheKey = namespaceName + ":" + biomeId;
    std::lock_guard<std::mutex> lock(GlobalCache::cacheMutex);
    auto it = GlobalCache::biomes.find(cacheKey);
    if (it != GlobalCache::biomes.end()) {
        return it->second;
    }

    std::cerr << "Biome JSON not found: " << cacheKey << std::endl;
    return nlohmann::json();
}

std::string Biome::GetColormapData(const std::string& namespaceName, const std::string& colormapName) {
    std::string cacheKey = namespaceName + ":" + colormapName;
    std::lock_guard<std::mutex> lock(GlobalCache::cacheMutex);

    auto it = GlobalCache::colormaps.find(cacheKey);
    if (it != GlobalCache::colormaps.end()) {
        std::string filePath;
        if (SaveColormapToFile(it->second, namespaceName, colormapName, filePath)) {
            return filePath;
        }
        else {
            std::cerr << "Failed to save colormap: " << cacheKey << std::endl;
            return "";
        }
    }

    std::cerr << "Colormap not found: " << cacheKey << std::endl;
    return "";
}

std::vector<std::vector<int>> Biome::GenerateBiomeMap(int minX, int minZ, int maxX, int maxZ, int y) {
    std::vector<std::vector<int>> biomeMap;
    int width = maxX - minX + 1;
    int height = maxZ - minZ + 1;

    biomeMap.resize(height, std::vector<int>(width));

    for (int x = minX; x <= maxX; ++x) {
        for (int z = minZ; z <= maxZ; ++z) {
            int currentY = GetHeightMapY(x, z, "MOTION_BLOCKING");
            int biomeId = GetBiomeId(x, currentY, z);
            biomeMap[z - minZ][x - minX] = biomeId; // 修正坐标映射
        }
    }
    return biomeMap;
}

BiomeColors Biome::ParseBiomeColors(const nlohmann::json& biomeJson) {
    BiomeColors colors;

    auto ParseColorWithFallback = [&](const std::string& key,
        const std::string& colormapType,
        float tempMod = 1.0f,
        float downfallMod = 1.0f) {
            int directColor = biomeJson["effects"].value(key, -1);
            if (directColor != -1) return directColor;

            auto colormap = GetColormapData("minecraft", colormapType);
            return CalculateColorFromColormap(colormap,
                colors.adjTemperature * tempMod,
                colors.adjDownfall * downfallMod);
        };


    // 检查 "temperature" 和 "downfall" 是否存在
    if (biomeJson.contains("temperature") && biomeJson.contains("downfall")) {
        const float temp = biomeJson["temperature"].get<float>();
        const float downfall = biomeJson["downfall"].get<float>();

        // 需要色图计算的参数准备
        colors.adjTemperature = BiomeUtils::clamp(temp, 0.0f, 1.0f);
        colors.adjDownfall = BiomeUtils::clamp(downfall, 0.0f, 1.0f);
    }
    else {
        // 如果没有提供温度和降水量，则设置默认值
        colors.adjTemperature = 0.5f;
        colors.adjDownfall = 0.5f;
    }

    // 检查 "effects" 是否存在
    if (biomeJson.contains("effects")) {
        const nlohmann::json& effects = biomeJson["effects"];

        // 解析直接颜色值
        colors.fog = effects.value("fog_color", 0xFFFFFF);
        colors.sky = effects.value("sky_color", 0x84ECFF);
        colors.water = effects.value("water_color", 0x3F76E4);
        colors.waterFog = effects.value("water_fog_color", 0x050533);
        // 统一处理植物颜色
        colors.foliage = ParseColorWithFallback("foliage_color", "foliage");
        colors.dryFoliage = ParseColorWithFallback("dry_foliage_color", "foliage", 1.2f, 0.8f);
        colors.grass = ParseColorWithFallback("grass_color", "grass");

    }



    return colors;
}

void Biome::PrintAllRegisteredBiomes() {
    std::lock_guard<std::shared_mutex> lock(registryMutex);

    std::cout << "已注册生物群系 (" << biomeRegistry.size() << " 个):\n";
    for (const auto& entry : biomeRegistry) {
        std::cout << "  ID: " << entry.second.id
            << "\t名称: " << entry.first << "\n";

    }


}

int Biome::GetId(const std::string& fullName) {
    std::unique_lock<std::shared_mutex> lock(registryMutex);

    // 验证命名格式
    const size_t colonPos = fullName.find(':');
    if (colonPos == std::string::npos) {
        throw std::invalid_argument("Invalid biome format: " + fullName);
    }
    auto it = biomeRegistry.find(fullName);
    // 已存在直接返回
    if (it != biomeRegistry.end()) {
        return it->second.id;
    }

    // 获取生物群系配置数据
    const auto& biomeJson = GetBiomeJson(fullName.substr(0, colonPos), fullName.substr(colonPos + 1));

    // 提前解析颜色数据
    BiomeColors colors = ParseBiomeColors(biomeJson);

    // 原子化注册操作
    auto& newBiome = biomeRegistry.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(fullName),
        std::forward_as_tuple(
            static_cast<int>(biomeRegistry.size()),
            std::move(colors)
        )
    ).first->second;

    // 设置基础信息
    newBiome.namespaceName = fullName.substr(0, colonPos);
    newBiome.biomeName = fullName.substr(colonPos + 1);

    return newBiome.id;
}

int Biome::GetColor(int biomeId, BiomeColorType colorType) {
    // 共享读锁
    std::shared_lock<std::shared_mutex> lock(registryMutex);

    auto it = std::find_if(biomeRegistry.begin(), biomeRegistry.end(),
        [biomeId](const auto& entry) { return entry.second.id == biomeId; });

    if (it == biomeRegistry.end()) return 0xFFFFFF;

    // 获取独立颜色锁
    std::lock_guard<std::mutex> colorLock(it->second.colorMutex);
    switch (colorType) {
    case BiomeColorType::Foliage: return it->second.colors.foliage;
    case BiomeColorType::DryFoliage: return it->second.colors.dryFoliage;
    case BiomeColorType::Grass: return it->second.colors.grass;
    case BiomeColorType::Fog:    return it->second.colors.fog;
    case BiomeColorType::Sky:    return it->second.colors.sky;
    case BiomeColorType::Water:  return it->second.colors.water;
    case BiomeColorType::WaterFog: return it->second.colors.waterFog;
    default: return 0xFFFFFF; // 白色作为默认错误颜色
    }
}

// 新增辅助函数处理颜色逻辑
int Biome::ParseColorWithFallback(nlohmann::json& effects,
    const std::string& colorKey,
    const std::string& colormapType, float Temperature, float Downfall,
    float tempModifier,
    float downfallModifier)
{
    if (effects.contains(colorKey)) {
        return effects[colorKey].get<int>();
    }

    // 若JSON中无颜色，立即计算并缓存
    auto colormap = Biome::GetColormapData("minecraft", colormapType);
    return CalculateColorFromColormap(colormap,
        Temperature * tempModifier,
        Downfall * downfallModifier);
}


int Biome::CalculateColorFromColormap(const std::string& filePath,
    float adjTemperature,
    float adjDownfall) {
    if (filePath.empty()) {
        return 0x00FF00; // 错误颜色
    }

    // 修改1：去掉强制RGBA参数（原第5个参数4改为0）
    int width, height, channels;
    unsigned char* data = stbi_load(filePath.c_str(),
        &width,
        &height,
        &channels,
        0); // 0表示保留原始通道数

    if (!data) {
        std::cerr << "Failed to load colormap: " << filePath
            << ", error: " << stbi_failure_reason() << std::endl;
        return 0x00FF00;
    }

    // 修改2：仅验证尺寸，允许不同通道数
    if (width != 256 || height != 256) {
        std::cerr << "Invalid colormap size: " << filePath
            << " (expected 256x256, got "
            << width << "x" << height << ")" << std::endl;
        stbi_image_free(data);
        return 0x00FF00;
    }

    // 修改3：根据实际通道数处理颜色数据
    const bool hasColorChannels = (channels >= 3);
    if (!hasColorChannels) {
        std::cerr << "Unsupported channel format: " << filePath
            << " (channels: " << channels << ")" << std::endl;
        stbi_image_free(data);
        return 0x00FF00;
    }

    // 坐标计算保持不变
    int x = static_cast<int>((1.0f - adjTemperature) * 255.0f);
    int y = static_cast<int>((1.0f - adjDownfall) * 255.0f);
    x = BiomeUtils::clamp(x, 0, 255);
    y = BiomeUtils::clamp(y, 0, 255);
    y = 255 - y; // 保持Y轴翻转

    // 修改4：动态计算像素偏移
    const size_t pixelOffset = (y * width + x) * channels;

    // 修改5：根据通道数提取颜色
    uint8_t r = data[pixelOffset];
    uint8_t g = data[pixelOffset + (channels >= 2 ? 1 : 0)]; // 单通道时复用R
    uint8_t b = data[pixelOffset + (channels >= 3 ? 2 : 0)]; // 双通道时复用R

    stbi_image_free(data);

    return (r << 16) | (g << 8) | b;
}

bool Biome::ExportToPNG(const std::vector<std::vector<int>>& biomeMap,
    const std::string& filename,
    BiomeColorType colorType)
{
    // 生成颜色映射
    std::map<int, std::tuple<uint8_t, uint8_t, uint8_t>> colorMap;

    for (const auto& entry : biomeRegistry) {
        int colorValue = GetColor(entry.second.id, colorType);
        uint8_t r = (colorValue >> 16) & 0xFF;
        uint8_t g = (colorValue >> 8) & 0xFF;
        uint8_t b = colorValue & 0xFF;
        //std::cout << std::to_string(r) <<"," << std::to_string(g) << "," << std::to_string(b) << std::endl;
        colorMap[entry.second.id] = std::make_tuple(r, g, b);
    }

    if (biomeMap.empty()) return false;

    const int height = biomeMap.size();
    const int width = biomeMap[0].size();

    // 尺寸校验
    for (const auto& row : biomeMap) {
        if (row.size() != static_cast<size_t>(width)) {
            std::cerr << "Error: Invalid biome map dimensions" << std::endl;
            return false;
        }
    }

    std::vector<uint8_t> imageData(width * height * 3);

    // 构建颜色映射（包含默认随机颜色生成）
    std::map<int, std::tuple<uint8_t, uint8_t, uint8_t>> finalColorMap = colorMap;

    // 解决方案：添加维度校验
    for (const auto& row : biomeMap) {
        if (row.size() != static_cast<size_t>(width)) {
            std::cerr << "Error: Biome map is not rectangular\n";
            return false;
        }
    }
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int biomeId = biomeMap[y][x];

            auto& color = finalColorMap[biomeId];
            int index = (y * width + x) * 3;
            imageData[index] = std::get<0>(color);  // R
            imageData[index + 1] = std::get<1>(color);  // G
            imageData[index + 2] = std::get<2>(color);  // B
        }
    }

    // 写入PNG文件
    return stbi_write_png(filename.c_str(), width, height, 3, imageData.data(), width * 3);
}