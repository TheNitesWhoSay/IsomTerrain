#include "IsomTests.h"
#include "IsomApi.h"
#include "../CrossCutLib/Logger.h"
#include "../CrossCutLib/SimpleIcu.h"
#include "../MappingCoreLib/MappingCore.h"
#include <iomanip>
#include <iostream>
#include <string>
#include <Windows.h>
#include <unordered_set>

Logger logger(LogLevel::Warn);

Sc::Terrain_ terrainDat;

enum class EnumFilesResult { Success, PartialSuccess, Failure };

template <typename FileFound>
EnumFilesResult EnumDirectoryFiles(const std::string &directoryPath, FileFound && fileFound)
{
    WIN32_FIND_DATA findData = {};
    HANDLE hFind = FindFirstFile(icux::toFilestring(directoryPath + "\\*").c_str(), &findData);
    if ( hFind != INVALID_HANDLE_VALUE )
    {
        EnumFilesResult result = EnumFilesResult::Success;
        do
        {
            if ( (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY )
            {
                if ( strcmp(icux::toUtf8(findData.cFileName).c_str(), ".") != 0 && strcmp(icux::toUtf8(findData.cFileName).c_str(), "..") != 0 )
                {
                    const std::string subDirectory(directoryPath + "\\" + icux::toUtf8(findData.cFileName));

                    if ( EnumDirectoryFiles(subDirectory, fileFound)
                        != EnumFilesResult::Success )
                    {
                        result = EnumFilesResult::PartialSuccess;
                    }
                }
            }
            else
                fileFound(directoryPath + '\\' + icux::toUtf8(findData.cFileName));

        } while ( FindNextFile(hFind, &findData) != 0 );

        if ( GetLastError() != ERROR_NO_MORE_FILES )
            result = EnumFilesResult::PartialSuccess;

        FindClose(hFind);
        return result;
    }
    return EnumFilesResult::Failure;
}

struct PlaceTerrainOp {
    size_t terrainType;
    size_t x; // x is a tileCoord/2, only even x-coords are valid on even y coords, only odd x-coords are valid on odd y-coords
    size_t y;
    size_t brushSize = 1;
};

// This is unnecessary overhead and isom editing should be built into a map object in a real impl, but the hard separation helps demo this separately
ScMap copyToScMap(const MapFile & src)
{
    ScMap dest {};
    dest.tileWidth = uint16_t(src.getTileWidth());
    dest.tileHeight = uint16_t(src.getTileHeight());
    dest.tileset = src.getTileset();
    dest.isomRects.assign(src.isomRects.size(), {});
    std::memcpy(&dest.isomRects[0], &src.isomRects[0], src.isomRects.size()*sizeof(Chk::IsomRect)); // ISOM
    dest.editorTiles = src.editorTiles; // TILE
    dest.tiles = src.tiles; // MTXM
    return dest;
}

void copyFromScMap(MapFile & dest, const ScMap & src)
{
    dest.dimensions.tileWidth = src.tileWidth;
    dest.dimensions.tileHeight = src.tileHeight;
    dest.tileset = src.tileset;
    dest.isomRects.assign(src.isomRects.size(), {});
    std::memcpy(&dest.isomRects[0], &src.isomRects[0], src.isomRects.size()*sizeof(Chk::IsomRect)); // ISOM
    dest.editorTiles = src.editorTiles; // TILE
    dest.tiles = src.tiles; // MTXM
}

std::unique_ptr<MapFile> openMap(const std::string & mapFilePath)
{
    // Could alternatively use MapFile(MapFile::getDefaultOpenMapBrowser());
    auto mapFile = std::make_unique<MapFile>(mapFilePath);
    return mapFile && !mapFile->empty() ? std::move(mapFile) : nullptr;
}

std::unique_ptr<MapFile> newMap(Sc::Terrain::Tileset tileset, uint16_t width, uint16_t height, size_t terrainType)
{
    auto mapFile = std::make_unique<MapFile>(tileset, width, height);
    ScMap scMap = copyToScMap(*mapFile);

    Chk::IsomCache isomCache(tileset, width, height, terrainDat.get(tileset));

    uint16_t isomValue = ((isomCache.getTerrainTypeIsomValue(terrainType) << 4) | Chk::IsomRect::EditorFlag::Modified);
    scMap.isomRects.assign(scMap.getIsomWidth()*scMap.getIsomHeight(), Chk::IsomRect{isomValue, isomValue, isomValue, isomValue});

    isomCache.setAllChanged();
    scMap.updateTilesFromIsom(isomCache);
    
    copyFromScMap(*mapFile, scMap);
    return std::move(mapFile);
}

// isomX is a tileCoordinate/2, only even x-coordinates are valid on even y coordinates, only odd x-coordinates are valid on odd y-coordinates
// isomBrush is one of the values from IsomBrush.h, e.g. Sc::Isom::Brush::Badlands::Dirt
bool placeTerrain(MapFile & mapFile, size_t terrainType, size_t isomX, size_t isomY, size_t brushSize)
{
    ScMap scMap = copyToScMap(mapFile);
    Chk::IsomCache isomCache(scMap.tileset, scMap.tileWidth, scMap.tileHeight, terrainDat.get(mapFile.tileset));
    scMap.placeIsomTerrain({isomX, isomY}, terrainType, brushSize, isomCache);
    scMap.updateTilesFromIsom(isomCache);
    copyFromScMap(mapFile, scMap);
    return true;
}

bool placeTerrain(MapFile & mapFile, const std::vector<PlaceTerrainOp> & ops)
{
    for ( const auto & op : ops )
    {
        if ( !placeTerrain(mapFile, op.terrainType, op.x, op.y, op.brushSize) )
            return false;
    }
    return true;
}

void setMtxmOrTileDimensions(std::vector<u16> & tiles, u16 newTileWidth, u16 newTileHeight, u16 oldTileWidth, u16 oldTileHeight, s32 leftEdge, s32 topEdge);

bool resizeMap(MapFile & mapFile, uint16_t newWidth, uint16_t newHeight, int xOffset, int yOffset, size_t terrainType)
{
    ScMap scMap = copyToScMap(mapFile);
    ScMap destMap {};
    destMap.tileset = scMap.tileset;
    destMap.tileWidth = scMap.tileWidth;
    destMap.tileHeight = scMap.tileHeight;
    Chk::IsomCache destIsomCache(scMap.tileset, newWidth, newHeight, terrainDat.get(scMap.tileset));

    destMap.editorTiles = scMap.editorTiles;
    destMap.tiles = scMap.tiles;
    setMtxmOrTileDimensions(destMap.tiles, newWidth, newHeight, (uint16_t)scMap.tileWidth, (uint16_t)scMap.tileHeight, 0, 0);
    setMtxmOrTileDimensions(destMap.editorTiles, newWidth, newHeight, (uint16_t)scMap.tileWidth, (uint16_t)scMap.tileHeight, 0, 0);
    uint16_t isomValue = ((destIsomCache.getTerrainTypeIsomValue(terrainType) << 4) | Chk::IsomRect::EditorFlag::Modified);

    destMap.tileWidth = newWidth;
    destMap.tileHeight = newHeight;
    destMap.isomRects.assign((newWidth/2+1)*(newHeight+1), Chk::IsomRect{ isomValue, isomValue, isomValue, isomValue });
    
    destMap.copyIsomFrom(scMap, xOffset, yOffset, false, destIsomCache);
    destMap.resizeIsom(xOffset, yOffset, scMap.tileWidth, scMap.tileHeight, false, destIsomCache);
    destMap.updateTilesFromIsom(destIsomCache);

    Sc::BoundingBox tileRect { scMap.tileWidth, scMap.tileHeight, newWidth, newHeight, xOffset, yOffset };
    size_t destStartX = xOffset < 0 ? 0 : xOffset;
    size_t destStartY = yOffset < 0 ? 0 : yOffset;
    size_t copyHeight = tileRect.bottom-tileRect.top;
    size_t copyWidth = tileRect.right-tileRect.left;
    for ( size_t y=0; y<copyHeight; ++y )
    {
        for ( size_t x=0; x<copyWidth; ++x )
        {
            destMap.editorTiles[(y+destStartY)*newWidth+(x+destStartX)] = scMap.editorTiles[(y+tileRect.top)*scMap.tileWidth+(x+tileRect.left)];
            destMap.tiles[(y+destStartY)*newWidth+(x+destStartX)] = scMap.tiles[(y+tileRect.top)*scMap.tileWidth+(x+tileRect.left)];
        }
    }

    copyFromScMap(mapFile, destMap);
    return true;
}

void resizeMapTest(const std::string & inputMap, const std::string & comparisonMap, uint16_t width, uint16_t height,
    int xOffset, int yOffset, size_t terrainType, size_t & resizeMapTestPassCount, size_t & resizeMapTestFailCount)
{
    if ( auto mapFile = openMap(inputMap) )
    {
        resizeMap(*mapFile, width, height, xOffset, yOffset, terrainType);
        if ( auto comparison = openMap(comparisonMap) )
        {
            if ( mapFile->isomRects.size() != comparison->isomRects.size() ||
                mapFile->editorTiles.size() != comparison->editorTiles.size() ||
                mapFile->tiles.size() != comparison->tiles.size() )
            {
                throw std::logic_error("Section size mismatch");
            }

            size_t isomMismatchCount = 0;
            for ( size_t i=0; i<mapFile->isomRects.size(); ++i )
            {
                if ( mapFile->isomRects[i].left != comparison->isomRects[i].left )
                    isomMismatchCount++;
                if ( mapFile->isomRects[i].top != comparison->isomRects[i].top )
                    isomMismatchCount++;
                if ( mapFile->isomRects[i].right != comparison->isomRects[i].right )
                    isomMismatchCount++;
                if ( mapFile->isomRects[i].bottom != comparison->isomRects[i].bottom )
                    isomMismatchCount++;
            }
            if ( isomMismatchCount == 0 )
            {
                std::cout << "PASS - Resize map perfect ISOM - " << mapFile->getFileName() << " - " << comparison->getFileName() << std::endl;
                resizeMapTestPassCount++;
            }
            else
            {
                std::cout << "FAIL - Resize map ISOM mismatch - " << mapFile->getFileName() << " - " << comparison->getFileName() << std::endl;
                resizeMapTestFailCount++;
            }

            size_t tileMismatchCount = 0;
            for ( size_t i=0; i<mapFile->editorTiles.size(); ++i )
            {
                if ( Sc::Terrain::getTileGroup(mapFile->editorTiles[i]) != Sc::Terrain::getTileGroup(comparison->editorTiles[i]) )
                    tileMismatchCount++;
            }
            if ( tileMismatchCount == 0 )
            {
                std::cout << "PASS - Resize map perfect TILE - " << mapFile->getFileName() << " - " << comparison->getFileName() << std::endl;
                resizeMapTestPassCount++;
            }
            else
            {
                std::cout << "FAIL - Resize map TILE mismatch - " << mapFile->getFileName() << " - " << comparison->getFileName() << std::endl;
                resizeMapTestFailCount++;
            }

            size_t mtxmMismatchCount = 0;
            for ( size_t i=0; i<mapFile->tiles.size(); ++i )
            {
                if ( Sc::Terrain::getTileGroup(mapFile->tiles[i]) != Sc::Terrain::getTileGroup(comparison->tiles[i]) )
                    mtxmMismatchCount++;
            }
            if ( mtxmMismatchCount == 0 )
            {
                std::cout << "PASS - Resize map perfect MTXM - " << mapFile->getFileName() << " - " << comparison->getFileName() << std::endl;
                resizeMapTestPassCount++;
            }
            else
            {
                std::cout << "FAIL - Resize map MTXM mismatch - " << mapFile->getFileName() << " - " << comparison->getFileName() << std::endl;
                resizeMapTestFailCount++;
            }
        }
        else
            throw std::logic_error("Failed to open comparison map");
    }
    else
        throw std::logic_error("Failed to open source map");
}

void editMapTest(const std::string & inputMap, const std::string & comparisonMap,
    size_t & editMapTestPassCount, size_t & editMapTestFailCount, const std::vector<PlaceTerrainOp> & ops)
{
    if ( auto mapFile = openMap(inputMap) )
    {
        placeTerrain(*mapFile, ops);
        if ( auto comparison = openMap(comparisonMap) )
        {
            if ( mapFile->isomRects.size() != comparison->isomRects.size() ||
                mapFile->editorTiles.size() != comparison->editorTiles.size() ||
                mapFile->tiles.size() != comparison->tiles.size() )
            {
                throw std::logic_error("Section size mismatch");
            }

            size_t isomMismatchCount = 0;
            for ( size_t i=0; i<mapFile->isomRects.size(); ++i )
            {
                if ( mapFile->isomRects[i].left != comparison->isomRects[i].left )
                    isomMismatchCount++;
                if ( mapFile->isomRects[i].top != comparison->isomRects[i].top )
                    isomMismatchCount++;
                if ( mapFile->isomRects[i].right != comparison->isomRects[i].right )
                    isomMismatchCount++;
                if ( mapFile->isomRects[i].bottom != comparison->isomRects[i].bottom )
                    isomMismatchCount++;
            }
            if ( isomMismatchCount == 0 )
            {
                std::cout << "PASS - Edit map perfect ISOM - " << mapFile->getFileName() << " - " << comparison->getFileName() << std::endl;
                editMapTestPassCount++;
            }
            else
            {
                std::cout << "FAIL - Edit map ISOM mismatch - " << mapFile->getFileName() << " - " << comparison->getFileName() << std::endl;
                editMapTestFailCount++;
            }

            size_t tileMismatchCount = 0;
            for ( size_t i=0; i<mapFile->editorTiles.size(); ++i )
            {
                if ( Sc::Terrain::getTileGroup(mapFile->editorTiles[i]) != Sc::Terrain::getTileGroup(comparison->editorTiles[i]) )
                    tileMismatchCount++;
            }
            if ( tileMismatchCount == 0 )
            {
                std::cout << "PASS - Edit map perfect TILE - " << mapFile->getFileName() << " - " << comparison->getFileName() << std::endl;
                editMapTestPassCount++;
            }
            else
            {
                std::cout << "FAIL - Edit map TILE mismatch - " << mapFile->getFileName() << " - " << comparison->getFileName() << std::endl;
                editMapTestFailCount++;
            }

            size_t mtxmMismatchCount = 0;
            for ( size_t i=0; i<mapFile->tiles.size(); ++i )
            {
                if ( Sc::Terrain::getTileGroup(mapFile->tiles[i]) != Sc::Terrain::getTileGroup(comparison->tiles[i]) )
                    mtxmMismatchCount++;
            }
            if ( mtxmMismatchCount == 0 )
            {
                std::cout << "PASS - Edit map perfect MTXM - " << mapFile->getFileName() << " - " << comparison->getFileName() << std::endl;
                editMapTestPassCount++;
            }
            else
            {
                std::cout << "FAIL - Edit map MTXM mismatch - " << mapFile->getFileName() << " - " << comparison->getFileName() << std::endl;
                editMapTestFailCount++;
            }

            //if ( comparisonMap.find("ScatterTest.scm") != std::string::npos ) // TODO: Temp
            //    mapFile->save(std::string("C:\\Users\\Justin\\Desktop\\output.scm"), true);
        }
        else
            throw std::logic_error("Failed to open comparison map");
    }
    else
        throw std::logic_error("Failed to open edit source map");
}

std::string getTestMapDirectory()
{
    std::string directory = *getModuleDirectory();
    if ( isDirectory(makeSystemFilePath(directory, "Map Testing Pack")) )
        directory = makeSystemFilePath(directory, "Map Testing Pack");
    else if ( isDirectory(makeSystemFilePath(getSystemFileDirectory(directory), "Map Testing Pack")) )
        directory = makeSystemFilePath(getSystemFileDirectory(directory), "Map Testing Pack");
    else if ( isDirectory(makeSystemFilePath(getSystemFileDirectory(getSystemFileDirectory(directory, false)), "Map Testing Pack")) )
        directory = makeSystemFilePath(getSystemFileDirectory(getSystemFileDirectory(directory, false)), "Map Testing Pack");
    else
        throw std::logic_error("Could not find test map directory.");

    return directory;
}

void runNewMapTests(const std::string & mapDir, size_t & newMapTestPassCount, size_t & newMapTestFailCount)
{
    std::unordered_map<std::string, Sc::Terrain::Tileset> directoryToTileset {
        {"Badlands", Sc::Terrain::Tileset::Badlands},
        {"Space", Sc::Terrain::Tileset::SpacePlatform},
        {"Installation", Sc::Terrain::Tileset::Installation},
        {"Ashworld", Sc::Terrain::Tileset::Ashworld},
        {"Jungle", Sc::Terrain::Tileset::Jungle},
        {"Desert", Sc::Terrain::Tileset::Desert},
        {"Arctic", Sc::Terrain::Tileset::Arctic},
        {"Twilight", Sc::Terrain::Tileset::Twilight},
    };

    std::unordered_map<Sc::Terrain::Tileset, std::unordered_map<std::string, size_t>> tilesetToBrushNameToTerrainType {
        {Sc::Terrain::Tileset::Badlands, {}}, {Sc::Terrain::Tileset::SpacePlatform, {}},
        {Sc::Terrain::Tileset::Installation, {}}, {Sc::Terrain::Tileset::Ashworld, {}},
        {Sc::Terrain::Tileset::Jungle, {}}, {Sc::Terrain::Tileset::Desert, {}},
        {Sc::Terrain::Tileset::Arctic, {}}, {Sc::Terrain::Tileset::Twilight, {}}
    };
    for ( Sc::Terrain::Tileset tilesetIndex = Sc::Terrain::Tileset::Badlands; tilesetIndex <= Sc::Terrain::Tileset::Twilight; ++(uint16_t &)tilesetIndex )
    {
        const auto & brushes = terrainDat.get(tilesetIndex).brushes;
        for ( const auto & brush : brushes )
            tilesetToBrushNameToTerrainType.find(tilesetIndex)->second.insert(std::make_pair(brush.name, brush.index));
    }

    EnumDirectoryFiles(makeSystemFilePath(mapDir, "Clean New Maps"), [&](auto filePath) {
        auto fileName = ::getSystemFileName(filePath);
        auto extensionStart = fileName.find(".");
        if ( extensionStart == std::string::npos )
        {
            logger.error() << "No extension on filePath: " << filePath << std::endl;
            return;
        }
        auto brushName = fileName.substr(0, extensionStart);

        auto fileDirectory = ::getSystemFileDirectory(filePath, false);
        auto containingDirectoryName = ::getSystemFileName(fileDirectory);
        auto foundTileset = directoryToTileset.find(containingDirectoryName);
        if ( foundTileset == directoryToTileset.end() )
        {
            logger.error() << "Error on filePath: " << filePath << std::endl;
            return;
        }
        Sc::Terrain::Tileset tileset = foundTileset->second;
        auto & brushNameToBrush = tilesetToBrushNameToTerrainType.find(tileset)->second;
        auto foundBrush = brushNameToBrush.find(brushName);
        if ( foundBrush == brushNameToBrush.end() )
        {
            logger.error() << "Error finding brush on filePath: " << filePath << std::endl;
            return;
        }
        auto brush = foundBrush->second;
        
        std::map<uint16_t, size_t> isomValueCount {};
        std::map<uint16_t, size_t> tileGroupCount {};
        std::map<uint16_t, size_t> mtxmGroupCount {};
        if ( auto examine = openMap(filePath) )
        {
            for ( auto & isomRect : examine->isomRects )
            {
                isomValueCount[isomRect.left]++;
                isomValueCount[isomRect.top]++;
                isomValueCount[isomRect.right]++;
                isomValueCount[isomRect.bottom]++;
            }
            for ( auto tile : examine->editorTiles )
                tileGroupCount[Sc::Terrain::getTileGroup(tile)]++;
            for ( auto tile : examine->tiles )
                mtxmGroupCount[Sc::Terrain::getTileGroup(tile)]++;
        }

        uint16_t mostPresentIsom = 0;
        for ( auto & count : isomValueCount )
        {
            if ( count.second > 100 )
            {
                if ( mostPresentIsom != 0 )
                    throw std::logic_error("Multiple ISOM values highly present in source map");
                else
                    mostPresentIsom = count.first;
            }
        }
        uint16_t mostPresentEditorTileGroup = 0;
        uint16_t otherMostPresentEditorTileGroup = 0;
        for ( auto & count : tileGroupCount )
        {
            if ( count.second > 100 )
            {
                if ( mostPresentEditorTileGroup == 0 )
                    mostPresentEditorTileGroup = count.first;
                else if ( otherMostPresentEditorTileGroup == 0 )
                    otherMostPresentEditorTileGroup = count.first;
                else
                    throw std::logic_error("More than 2 TILE values highly present in source map");
            }
        }
        uint16_t mostPresentTileGroup = 0;
        uint16_t otherMostPresentTileGroup = 0;
        for ( auto & count : mtxmGroupCount )
        {
            if ( count.second > 100 )
            {
                if ( mostPresentTileGroup == 0 )
                    mostPresentTileGroup = count.first;
                else if ( otherMostPresentTileGroup == 0 )
                    otherMostPresentTileGroup = count.first;
                else
                    throw std::logic_error("More than 2 TILE values highly present in source map");
            }
        }
        
        size_t isomMismatchCount = 0;
        size_t tileMismatchCount = 0;
        size_t mtxmMismatchCount = 0;
        if ( auto mapFile = newMap(tileset, 128, 128, brush) )
        {
            for ( auto & isomRect : mapFile->isomRects )
            {
                if ( isomRect.left != mostPresentIsom )
                    isomMismatchCount ++;
                if ( isomRect.top != mostPresentIsom )
                    isomMismatchCount ++;
                if ( isomRect.right != mostPresentIsom )
                    isomMismatchCount ++;
                if ( isomRect.bottom != mostPresentIsom )
                    isomMismatchCount ++;
            }
            if ( isomMismatchCount == 0 )
            {
                std::cout << "PASS - New map perfect ISOM - " << containingDirectoryName << " - " << brushName << std::endl;
                newMapTestPassCount++;
            }
            else
            {
                std::cout << "FAIL - New map ISOM mismatch - " << containingDirectoryName << " - " << brushName << std::endl;
                newMapTestFailCount++;
            }

            for ( auto tile : mapFile->editorTiles )
            {
                auto group = Sc::Terrain::getTileGroup(tile);
                if ( tile == 0 || (group != mostPresentEditorTileGroup && group != otherMostPresentEditorTileGroup) )
                    tileMismatchCount++;
            }
            if ( tileMismatchCount == 0 )
            {
                std::cout << "PASS - New map perfect TILE - " << containingDirectoryName << " - " << brushName << std::endl;
                newMapTestPassCount++;
            }
            else
            {
                std::cout << "FAIL - New map TILE mismatch - " << containingDirectoryName << " - " << brushName << std::endl;
                newMapTestFailCount++;
            }

            for ( auto tile : mapFile->tiles )
            {
                auto group = Sc::Terrain::getTileGroup(tile);
                if ( tile == 0 || (group != mostPresentTileGroup && group != otherMostPresentTileGroup) )
                    mtxmMismatchCount++;
            }
            if ( mtxmMismatchCount == 0 )
            {
                std::cout << "PASS - New map perfect MTXM - " << containingDirectoryName << " - " << brushName << std::endl;
                newMapTestPassCount++;
            }
            else
            {
                std::cout << "FAIL - New map MTXM mismatch - " << containingDirectoryName << " - " << brushName << std::endl;
                newMapTestFailCount++;
            }
        }
        else
        {
            std::cout << "FAIL - New map creation error - " << containingDirectoryName << " - " << brushName << std::endl;
            newMapTestFailCount++;
        }
    });
    
    std::cout << "-----------------------------------------------------------------------" << std::endl;
}

void runResizeMapTests(const std::string & mapDir, size_t & resizeMapTestPassCount, size_t & resizeMapTestFailCount)
{
    resizeMapTest(mapDir + "\\Resize Source Maps\\dirt.scm",
        mapDir + "\\ScmDraft Resized Maps\\dirtToGrass64.scm",
        64, 64, 0, 0, Sc::Isom::Brush::Badlands::Grass, resizeMapTestPassCount, resizeMapTestFailCount);
    resizeMapTest(mapDir + "\\Resize Source Maps\\dirt.scm",
        mapDir + "\\ScmDraft Resized Maps\\dirtToGrass256.scm",
        256, 256, 0, 0, Sc::Isom::Brush::Badlands::Grass, resizeMapTestPassCount, resizeMapTestFailCount);
    resizeMapTest(mapDir + "\\Resize Source Maps\\dirt.scm",
        mapDir + "\\ScmDraft Resized Maps\\dirtToGrass256_p12_p9.scm",
        256, 256, 12, 9, Sc::Isom::Brush::Badlands::Grass, resizeMapTestPassCount, resizeMapTestFailCount);
    resizeMapTest(mapDir + "\\Resize Source Maps\\dirt.scm",
        mapDir + "\\ScmDraft Resized Maps\\dirtToGrass256_p12_m9.scm",
        256, 256, 12, -9, Sc::Isom::Brush::Badlands::Grass, resizeMapTestPassCount, resizeMapTestFailCount);
    resizeMapTest(mapDir + "\\Resize Source Maps\\dirt.scm",
        mapDir + "\\ScmDraft Resized Maps\\dirtToGrass256_m12_p9.scm",
        256, 256, -12, 9, Sc::Isom::Brush::Badlands::Grass, resizeMapTestPassCount, resizeMapTestFailCount);
    resizeMapTest(mapDir + "\\Resize Source Maps\\dirt.scm",
        mapDir + "\\ScmDraft Resized Maps\\dirtToGrass256_m12_m9.scm",
        256, 256, -12, -9, Sc::Isom::Brush::Badlands::Grass, resizeMapTestPassCount, resizeMapTestFailCount);
    
    resizeMapTest(mapDir + "\\Resize Source Maps\\Helms Deep Annatar East 8.7.scx",
        mapDir + "\\ScmDraft Resized Maps\\hde64.scm",
        64, 64, 0, 0, Sc::Isom::Brush::Jungle::Water, resizeMapTestPassCount, resizeMapTestFailCount);
    resizeMapTest(mapDir + "\\Resize Source Maps\\Helms Deep Annatar East 8.7.scx",
        mapDir + "\\ScmDraft Resized Maps\\hde256.scm",
        256, 256, 0, 0, Sc::Isom::Brush::Jungle::Water, resizeMapTestPassCount, resizeMapTestFailCount);
    resizeMapTest(mapDir + "\\Resize Source Maps\\Helms Deep Annatar East 8.7.scx",
        mapDir + "\\ScmDraft Resized Maps\\hde256_p12_p9.scm",
        256, 256, 12, 9, Sc::Isom::Brush::Jungle::Water, resizeMapTestPassCount, resizeMapTestFailCount);
    resizeMapTest(mapDir + "\\Resize Source Maps\\Helms Deep Annatar East 8.7.scx",
        mapDir + "\\ScmDraft Resized Maps\\hde256_m12_m9.scm",
        256, 256, -12, -9, Sc::Isom::Brush::Jungle::Water, resizeMapTestPassCount, resizeMapTestFailCount);

    std::cout << "-----------------------------------------------------------------------" << std::endl;
}

void runEditMapTests(const std::string & mapDir, size_t & editMapTestPassCount, size_t & editMapTestFailCount)
{
    editMapTest(mapDir + "\\Edit Source Maps\\Jungle.scm",
        mapDir + "\\ScmDraft Edited Maps\\LimitTest.scm", editMapTestPassCount, editMapTestFailCount,
        {
            {Sc::Isom::Brush::Jungle::HighTemple, 0, 0},
            {Sc::Isom::Brush::Jungle::HighTemple, 128, 0},
            {Sc::Isom::Brush::Jungle::HighTemple, 0, 256},
            {Sc::Isom::Brush::Jungle::HighTemple, 128, 256}
        });
    
    editMapTest(mapDir + "\\Edit Source Maps\\Jungle.scm",
        mapDir + "\\ScmDraft Edited Maps\\ScatterTest.scm", editMapTestPassCount, editMapTestFailCount,
        {
            {Sc::Isom::Brush::Jungle::HighTemple, 64, 128, 10},
            {Sc::Isom::Brush::Jungle::HighRaisedJungle, 67, 121, 1},
            {Sc::Isom::Brush::Jungle::HighRuins, 73, 127, 1},
            {Sc::Isom::Brush::Jungle::HighJungle, 70, 124, 1},
            {Sc::Isom::Brush::Jungle::HighDirt, 67, 125, 1},
            {Sc::Isom::Brush::Jungle::Temple, 53, 121, 1},
            {Sc::Isom::Brush::Jungle::RaisedJungle, 49, 125, 1},
            {Sc::Isom::Brush::Jungle::Ruins, 49, 129, 1},
            {Sc::Isom::Brush::Jungle::RockyGround, 54, 134, 1},
            {Sc::Isom::Brush::Jungle::Jungle_, 68, 136, 1},
            {Sc::Isom::Brush::Jungle::Mud, 63, 141, 1},
            {Sc::Isom::Brush::Jungle::Dirt, 55, 115, 1},
            {Sc::Isom::Brush::Jungle::Water, 77, 133, 1}
        });

    editMapTest(mapDir + "\\Clean New Maps\\Badlands\\Dirt.scm",
        mapDir + "\\ScmDraft Edited Maps\\Badlands\\Dirt.scm", editMapTestPassCount, editMapTestFailCount, {
            {Sc::Isom::Brush::Badlands::Dirt, 8, 16, 1},
            {Sc::Isom::Brush::Badlands::Dirt, 16, 16, 2},
            {Sc::Isom::Brush::Badlands::Dirt, 24, 16, 3},
            {Sc::Isom::Brush::Badlands::Mud, 8, 32, 1},
            {Sc::Isom::Brush::Badlands::Mud, 16, 32, 2},
            {Sc::Isom::Brush::Badlands::Mud, 24, 32, 3},
            {Sc::Isom::Brush::Badlands::HighDirt, 8, 48, 1},
            {Sc::Isom::Brush::Badlands::HighDirt, 16, 48, 2},
            {Sc::Isom::Brush::Badlands::HighDirt, 24, 48, 3},
            {Sc::Isom::Brush::Badlands::Water, 8, 64, 1},
            {Sc::Isom::Brush::Badlands::Water, 16, 64, 2},
            {Sc::Isom::Brush::Badlands::Water, 24, 64, 3},
            {Sc::Isom::Brush::Badlands::Grass, 8, 80, 1},
            {Sc::Isom::Brush::Badlands::Grass, 16, 80, 2},
            {Sc::Isom::Brush::Badlands::Grass, 24, 80, 3},
            {Sc::Isom::Brush::Badlands::HighGrass, 8, 96, 1},
            {Sc::Isom::Brush::Badlands::HighGrass, 16, 96, 2},
            {Sc::Isom::Brush::Badlands::HighGrass, 24, 96, 3},
            {Sc::Isom::Brush::Badlands::Structure, 8, 112, 1},
            {Sc::Isom::Brush::Badlands::Structure, 16, 112, 2},
            {Sc::Isom::Brush::Badlands::Structure, 24, 112, 3},
            {Sc::Isom::Brush::Badlands::Asphalt, 32, 16, 1},
            {Sc::Isom::Brush::Badlands::Asphalt, 40, 16, 2},
            {Sc::Isom::Brush::Badlands::Asphalt, 48, 16, 3},
            {Sc::Isom::Brush::Badlands::RockyGround, 32, 32, 1},
            {Sc::Isom::Brush::Badlands::RockyGround, 40, 32, 2},
            {Sc::Isom::Brush::Badlands::RockyGround, 48, 32, 3},
        });

    editMapTest(mapDir + "\\Clean New Maps\\Space\\Space.scm",
        mapDir + "\\ScmDraft Edited Maps\\Space\\Space.scm", editMapTestPassCount, editMapTestFailCount, {
            {Sc::Isom::Brush::Space::Space_, 8, 16, 1},
            {Sc::Isom::Brush::Space::Space_, 16, 16, 2},
            {Sc::Isom::Brush::Space::Space_, 24, 16, 3},
            {Sc::Isom::Brush::Space::LowPlatform, 8, 32, 1},
            {Sc::Isom::Brush::Space::LowPlatform, 16, 32, 2},
            {Sc::Isom::Brush::Space::LowPlatform, 24, 32, 3},
            {Sc::Isom::Brush::Space::RustyPit, 8, 48, 1},
            {Sc::Isom::Brush::Space::RustyPit, 16, 48, 2},
            {Sc::Isom::Brush::Space::RustyPit, 24, 48, 3},
            {Sc::Isom::Brush::Space::Platform, 8, 64, 1},
            {Sc::Isom::Brush::Space::Platform, 16, 64, 2},
            {Sc::Isom::Brush::Space::Platform, 24, 64, 3},
            {Sc::Isom::Brush::Space::DarkPlatform, 8, 80, 1},
            {Sc::Isom::Brush::Space::DarkPlatform, 16, 80, 2},
            {Sc::Isom::Brush::Space::DarkPlatform, 24, 80, 3},
            {Sc::Isom::Brush::Space::Plating, 8, 96, 1},
            {Sc::Isom::Brush::Space::Plating, 16, 96, 2},
            {Sc::Isom::Brush::Space::Plating, 24, 96, 3},
            {Sc::Isom::Brush::Space::SolarArray, 8, 112, 1},
            {Sc::Isom::Brush::Space::SolarArray, 16, 112, 2},
            {Sc::Isom::Brush::Space::SolarArray, 24, 112, 3},
            {Sc::Isom::Brush::Space::HighPlatform, 40, 16, 1},
            {Sc::Isom::Brush::Space::HighPlatform, 48, 16, 2},
            {Sc::Isom::Brush::Space::HighPlatform, 56, 16, 3},
            {Sc::Isom::Brush::Space::HighPlating, 40, 32, 1},
            {Sc::Isom::Brush::Space::HighPlating, 48, 32, 2},
            {Sc::Isom::Brush::Space::HighPlating, 56, 32, 3},
            {Sc::Isom::Brush::Space::ElevatedCatwalk, 40, 48, 1},
            {Sc::Isom::Brush::Space::ElevatedCatwalk, 48, 48, 2},
            {Sc::Isom::Brush::Space::ElevatedCatwalk, 56, 48, 3},
        });

    editMapTest(mapDir + "\\Clean New Maps\\Installation\\Substructure.scm",
        mapDir + "\\ScmDraft Edited Maps\\Installation\\Substructure.scm", editMapTestPassCount, editMapTestFailCount, {
            {Sc::Isom::Brush::Installation::Substructure, 8, 16, 1},
            {Sc::Isom::Brush::Installation::Substructure, 16, 16, 2},
            {Sc::Isom::Brush::Installation::Substructure, 24, 16, 3},
            {Sc::Isom::Brush::Installation::Floor, 8, 32, 1},
            {Sc::Isom::Brush::Installation::Floor, 16, 32, 2},
            {Sc::Isom::Brush::Installation::Floor, 24, 32, 3},
            {Sc::Isom::Brush::Installation::Roof, 8, 48, 1},
            {Sc::Isom::Brush::Installation::Roof, 16, 48, 2},
            {Sc::Isom::Brush::Installation::Roof, 24, 48, 3},
            {Sc::Isom::Brush::Installation::SubstructurePlating, 8, 64, 1},
            {Sc::Isom::Brush::Installation::SubstructurePlating, 16, 64, 2},
            {Sc::Isom::Brush::Installation::SubstructurePlating, 24, 64, 3},
            {Sc::Isom::Brush::Installation::Plating, 8, 80, 1},
            {Sc::Isom::Brush::Installation::Plating, 16, 80, 2},
            {Sc::Isom::Brush::Installation::Plating, 24, 80, 3},
            {Sc::Isom::Brush::Installation::SubstructurePanels, 8, 96, 1},
            {Sc::Isom::Brush::Installation::SubstructurePanels, 16, 96, 2},
            {Sc::Isom::Brush::Installation::SubstructurePanels, 24, 96, 3},
            {Sc::Isom::Brush::Installation::BottomlessPit, 8, 112, 1},
            {Sc::Isom::Brush::Installation::BottomlessPit, 16, 112, 2},
            {Sc::Isom::Brush::Installation::BottomlessPit, 24, 112, 3}
        });

    editMapTest(mapDir + "\\Clean New Maps\\Ashworld\\Magma.scm",
        mapDir + "\\ScmDraft Edited Maps\\Ashworld\\Magma.scm", editMapTestPassCount, editMapTestFailCount, {
            {Sc::Isom::Brush::Ashworld::Magma, 8, 16, 1},
            {Sc::Isom::Brush::Ashworld::Magma, 16, 16, 2},
            {Sc::Isom::Brush::Ashworld::Magma, 24, 16, 3},
            {Sc::Isom::Brush::Ashworld::Dirt, 8, 32, 1},
            {Sc::Isom::Brush::Ashworld::Dirt, 16, 32, 2},
            {Sc::Isom::Brush::Ashworld::Dirt, 24, 32, 3},
            {Sc::Isom::Brush::Ashworld::Lava, 8, 48, 1},
            {Sc::Isom::Brush::Ashworld::Lava, 16, 48, 2},
            {Sc::Isom::Brush::Ashworld::Lava, 24, 48, 3},
            {Sc::Isom::Brush::Ashworld::Shale, 8, 64, 1},
            {Sc::Isom::Brush::Ashworld::Shale, 16, 64, 2},
            {Sc::Isom::Brush::Ashworld::Shale, 24, 64, 3},
            {Sc::Isom::Brush::Ashworld::BrokenRock, 8, 80, 1},
            {Sc::Isom::Brush::Ashworld::BrokenRock, 16, 80, 2},
            {Sc::Isom::Brush::Ashworld::BrokenRock, 24, 80, 3},
            {Sc::Isom::Brush::Ashworld::HighDirt, 8, 96, 1},
            {Sc::Isom::Brush::Ashworld::HighDirt, 16, 96, 2},
            {Sc::Isom::Brush::Ashworld::HighDirt, 24, 96, 3},
            {Sc::Isom::Brush::Ashworld::HighLava, 8, 112, 1},
            {Sc::Isom::Brush::Ashworld::HighLava, 16, 112, 2},
            {Sc::Isom::Brush::Ashworld::HighLava, 24, 112, 3},
            {Sc::Isom::Brush::Ashworld::HighShale, 40, 16, 1},
            {Sc::Isom::Brush::Ashworld::HighShale, 48, 16, 2},
            {Sc::Isom::Brush::Ashworld::HighShale, 56, 16, 3},
        });

    editMapTest(mapDir + "\\Clean New Maps\\Jungle\\Water.scm",
        mapDir + "\\ScmDraft Edited Maps\\Jungle\\Water.scm", editMapTestPassCount, editMapTestFailCount, {
            {Sc::Isom::Brush::Jungle::Water, 8, 16, 1},
            {Sc::Isom::Brush::Jungle::Water, 16, 16, 2},
            {Sc::Isom::Brush::Jungle::Water, 24, 16, 3},
            {Sc::Isom::Brush::Jungle::Dirt, 8, 32, 1},
            {Sc::Isom::Brush::Jungle::Dirt, 16, 32, 2},
            {Sc::Isom::Brush::Jungle::Dirt, 24, 32, 3},
            {Sc::Isom::Brush::Jungle::Mud, 8, 48, 1},
            {Sc::Isom::Brush::Jungle::Mud, 16, 48, 2},
            {Sc::Isom::Brush::Jungle::Mud, 24, 48, 3},
            {Sc::Isom::Brush::Jungle::Jungle_, 8, 64, 1},
            {Sc::Isom::Brush::Jungle::Jungle_, 16, 64, 2},
            {Sc::Isom::Brush::Jungle::Jungle_, 24, 64, 3},
            {Sc::Isom::Brush::Jungle::RockyGround, 8, 80, 1},
            {Sc::Isom::Brush::Jungle::RockyGround, 16, 80, 2},
            {Sc::Isom::Brush::Jungle::RockyGround, 24, 80, 3},
            {Sc::Isom::Brush::Jungle::Ruins, 8, 96, 1},
            {Sc::Isom::Brush::Jungle::Ruins, 16, 96, 2},
            {Sc::Isom::Brush::Jungle::Ruins, 24, 96, 3},
            {Sc::Isom::Brush::Jungle::RaisedJungle, 8, 112, 1},
            {Sc::Isom::Brush::Jungle::RaisedJungle, 16, 112, 2},
            {Sc::Isom::Brush::Jungle::RaisedJungle, 24, 112, 3},
            {Sc::Isom::Brush::Jungle::Temple, 40, 16, 1},
            {Sc::Isom::Brush::Jungle::Temple, 48, 16, 2},
            {Sc::Isom::Brush::Jungle::Temple, 56, 16, 3},
            {Sc::Isom::Brush::Jungle::HighDirt, 40, 32, 1},
            {Sc::Isom::Brush::Jungle::HighDirt, 48, 32, 2},
            {Sc::Isom::Brush::Jungle::HighDirt, 56, 32, 3},
            {Sc::Isom::Brush::Jungle::HighJungle, 40, 48, 1},
            {Sc::Isom::Brush::Jungle::HighJungle, 48, 48, 2},
            {Sc::Isom::Brush::Jungle::HighJungle, 56, 48, 3},
            {Sc::Isom::Brush::Jungle::HighRuins, 40, 64, 1},
            {Sc::Isom::Brush::Jungle::HighRuins, 48, 64, 2},
            {Sc::Isom::Brush::Jungle::HighRuins, 56, 64, 3},
            {Sc::Isom::Brush::Jungle::HighRaisedJungle, 40, 80, 1},
            {Sc::Isom::Brush::Jungle::HighRaisedJungle, 48, 80, 2},
            {Sc::Isom::Brush::Jungle::HighRaisedJungle, 56, 80, 3},
            {Sc::Isom::Brush::Jungle::HighTemple, 40, 96, 1},
            {Sc::Isom::Brush::Jungle::HighTemple, 48, 96, 2},
            {Sc::Isom::Brush::Jungle::HighTemple, 56, 96, 3},
        });

    editMapTest(mapDir + "\\Clean New Maps\\Desert\\Tar.scx",
        mapDir + "\\ScmDraft Edited Maps\\Desert\\Tar.scx", editMapTestPassCount, editMapTestFailCount, {
            {Sc::Isom::Brush::Desert::Tar, 8, 16, 1},
            {Sc::Isom::Brush::Desert::Tar, 16, 16, 2},
            {Sc::Isom::Brush::Desert::Tar, 24, 16, 3},
            {Sc::Isom::Brush::Desert::Dirt, 8, 32, 1},
            {Sc::Isom::Brush::Desert::Dirt, 16, 32, 2},
            {Sc::Isom::Brush::Desert::Dirt, 24, 32, 3},
            {Sc::Isom::Brush::Desert::DriedMud, 8, 48, 1},
            {Sc::Isom::Brush::Desert::DriedMud, 16, 48, 2},
            {Sc::Isom::Brush::Desert::DriedMud, 24, 48, 3},
            {Sc::Isom::Brush::Desert::SandDunes, 8, 64, 1},
            {Sc::Isom::Brush::Desert::SandDunes, 16, 64, 2},
            {Sc::Isom::Brush::Desert::SandDunes, 24, 64, 3},
            {Sc::Isom::Brush::Desert::RockyGround, 8, 80, 1},
            {Sc::Isom::Brush::Desert::RockyGround, 16, 80, 2},
            {Sc::Isom::Brush::Desert::RockyGround, 24, 80, 3},
            {Sc::Isom::Brush::Desert::Crags, 8, 96, 1},
            {Sc::Isom::Brush::Desert::Crags, 16, 96, 2},
            {Sc::Isom::Brush::Desert::Crags, 24, 96, 3},
            {Sc::Isom::Brush::Desert::SandySunkenPit, 8, 112, 1},
            {Sc::Isom::Brush::Desert::SandySunkenPit, 16, 112, 2},
            {Sc::Isom::Brush::Desert::SandySunkenPit, 24, 112, 3},
            {Sc::Isom::Brush::Desert::Compound, 40, 16, 1},
            {Sc::Isom::Brush::Desert::Compound, 48, 16, 2},
            {Sc::Isom::Brush::Desert::Compound, 56, 16, 3},
            {Sc::Isom::Brush::Desert::HighDirt, 40, 32, 1},
            {Sc::Isom::Brush::Desert::HighDirt, 48, 32, 2},
            {Sc::Isom::Brush::Desert::HighDirt, 56, 32, 3},
            {Sc::Isom::Brush::Desert::HighSandDunes, 40, 48, 1},
            {Sc::Isom::Brush::Desert::HighSandDunes, 48, 48, 2},
            {Sc::Isom::Brush::Desert::HighSandDunes, 56, 48, 3},
            {Sc::Isom::Brush::Desert::HighCrags, 40, 64, 1},
            {Sc::Isom::Brush::Desert::HighCrags, 48, 64, 2},
            {Sc::Isom::Brush::Desert::HighCrags, 56, 64, 3},
            {Sc::Isom::Brush::Desert::HighSandySunkenPit, 40, 80, 1},
            {Sc::Isom::Brush::Desert::HighSandySunkenPit, 48, 80, 2},
            {Sc::Isom::Brush::Desert::HighSandySunkenPit, 56, 80, 3},
            {Sc::Isom::Brush::Desert::HighCompound, 40, 96, 1},
            {Sc::Isom::Brush::Desert::HighCompound, 48, 96, 2},
            {Sc::Isom::Brush::Desert::HighCompound, 56, 96, 3},
        });

    editMapTest(mapDir + "\\Clean New Maps\\Arctic\\Ice.scx",
        mapDir + "\\ScmDraft Edited Maps\\Arctic\\Ice.scx", editMapTestPassCount, editMapTestFailCount, {
            {Sc::Isom::Brush::Arctic::Ice, 8, 16, 1},
            {Sc::Isom::Brush::Arctic::Ice, 16, 16, 2},
            {Sc::Isom::Brush::Arctic::Ice, 24, 16, 3},
            {Sc::Isom::Brush::Arctic::Snow, 8, 32, 1},
            {Sc::Isom::Brush::Arctic::Snow, 16, 32, 2},
            {Sc::Isom::Brush::Arctic::Snow, 24, 32, 3},
            {Sc::Isom::Brush::Arctic::Moguls, 8, 48, 1},
            {Sc::Isom::Brush::Arctic::Moguls, 16, 48, 2},
            {Sc::Isom::Brush::Arctic::Moguls, 24, 48, 3},
            {Sc::Isom::Brush::Arctic::Dirt, 8, 64, 1},
            {Sc::Isom::Brush::Arctic::Dirt, 16, 64, 2},
            {Sc::Isom::Brush::Arctic::Dirt, 24, 64, 3},
            {Sc::Isom::Brush::Arctic::RockySnow, 8, 80, 1},
            {Sc::Isom::Brush::Arctic::RockySnow, 16, 80, 2},
            {Sc::Isom::Brush::Arctic::RockySnow, 24, 80, 3},
            {Sc::Isom::Brush::Arctic::Grass, 8, 96, 1},
            {Sc::Isom::Brush::Arctic::Grass, 16, 96, 2},
            {Sc::Isom::Brush::Arctic::Grass, 24, 96, 3},
            {Sc::Isom::Brush::Arctic::Water, 8, 112, 1},
            {Sc::Isom::Brush::Arctic::Water, 16, 112, 2},
            {Sc::Isom::Brush::Arctic::Water, 24, 112, 3},
            {Sc::Isom::Brush::Arctic::Outpost, 40, 16, 1},
            {Sc::Isom::Brush::Arctic::Outpost, 48, 16, 2},
            {Sc::Isom::Brush::Arctic::Outpost, 56, 16, 3},
            {Sc::Isom::Brush::Arctic::HighSnow, 40, 32, 1},
            {Sc::Isom::Brush::Arctic::HighSnow, 48, 32, 2},
            {Sc::Isom::Brush::Arctic::HighSnow, 56, 32, 3},
            {Sc::Isom::Brush::Arctic::HighDirt, 40, 48, 1},
            {Sc::Isom::Brush::Arctic::HighDirt, 48, 48, 2},
            {Sc::Isom::Brush::Arctic::HighDirt, 56, 48, 3},
            {Sc::Isom::Brush::Arctic::HighGrass, 40, 64, 1},
            {Sc::Isom::Brush::Arctic::HighGrass, 48, 64, 2},
            {Sc::Isom::Brush::Arctic::HighGrass, 56, 64, 3},
            {Sc::Isom::Brush::Arctic::HighWater, 40, 80, 1},
            {Sc::Isom::Brush::Arctic::HighWater, 48, 80, 2},
            {Sc::Isom::Brush::Arctic::HighWater, 56, 80, 3},
            {Sc::Isom::Brush::Arctic::HighOutpost, 40, 96, 1},
            {Sc::Isom::Brush::Arctic::HighOutpost, 48, 96, 2},
            {Sc::Isom::Brush::Arctic::HighOutpost, 56, 96, 3},
        });

    editMapTest(mapDir + "\\Clean New Maps\\Twilight\\Water.scx",
        mapDir + "\\ScmDraft Edited Maps\\Twilight\\Water.scx", editMapTestPassCount, editMapTestFailCount, {
            {Sc::Isom::Brush::Twilight::Water, 8, 16, 1},
            {Sc::Isom::Brush::Twilight::Water, 16, 16, 2},
            {Sc::Isom::Brush::Twilight::Water, 24, 16, 3},
            {Sc::Isom::Brush::Twilight::Dirt, 8, 32, 1},
            {Sc::Isom::Brush::Twilight::Dirt, 16, 32, 2},
            {Sc::Isom::Brush::Twilight::Dirt, 24, 32, 3},
            {Sc::Isom::Brush::Twilight::Mud, 8, 48, 1},
            {Sc::Isom::Brush::Twilight::Mud, 16, 48, 2},
            {Sc::Isom::Brush::Twilight::Mud, 24, 48, 3},
            {Sc::Isom::Brush::Twilight::CrushedRock, 8, 64, 1},
            {Sc::Isom::Brush::Twilight::CrushedRock, 16, 64, 2},
            {Sc::Isom::Brush::Twilight::CrushedRock, 24, 64, 3},
            {Sc::Isom::Brush::Twilight::Crevices, 8, 80, 1},
            {Sc::Isom::Brush::Twilight::Crevices, 16, 80, 2},
            {Sc::Isom::Brush::Twilight::Crevices, 24, 80, 3},
            {Sc::Isom::Brush::Twilight::Flagstones, 8, 96, 1},
            {Sc::Isom::Brush::Twilight::Flagstones, 16, 96, 2},
            {Sc::Isom::Brush::Twilight::Flagstones, 24, 96, 3},
            {Sc::Isom::Brush::Twilight::SunkenGround, 8, 112, 1},
            {Sc::Isom::Brush::Twilight::SunkenGround, 16, 112, 2},
            {Sc::Isom::Brush::Twilight::SunkenGround, 24, 112, 3},
            {Sc::Isom::Brush::Twilight::Basilica, 40, 16, 1},
            {Sc::Isom::Brush::Twilight::Basilica, 48, 16, 2},
            {Sc::Isom::Brush::Twilight::Basilica, 56, 16, 3},
            {Sc::Isom::Brush::Twilight::HighDirt, 40, 32, 1},
            {Sc::Isom::Brush::Twilight::HighDirt, 48, 32, 2},
            {Sc::Isom::Brush::Twilight::HighDirt, 56, 32, 3},
            {Sc::Isom::Brush::Twilight::HighCrushedRock, 40, 48, 1},
            {Sc::Isom::Brush::Twilight::HighCrushedRock, 48, 48, 2},
            {Sc::Isom::Brush::Twilight::HighCrushedRock, 56, 48, 3},
            {Sc::Isom::Brush::Twilight::HighFlagstones, 40, 64, 1},
            {Sc::Isom::Brush::Twilight::HighFlagstones, 48, 64, 2},
            {Sc::Isom::Brush::Twilight::HighFlagstones, 56, 64, 3},
            {Sc::Isom::Brush::Twilight::HighSunkenGround, 40, 80, 1},
            {Sc::Isom::Brush::Twilight::HighSunkenGround, 48, 80, 2},
            {Sc::Isom::Brush::Twilight::HighSunkenGround, 56, 80, 3},
            {Sc::Isom::Brush::Twilight::HighBasilica, 40, 96, 1},
            {Sc::Isom::Brush::Twilight::HighBasilica, 48, 96, 2},
            {Sc::Isom::Brush::Twilight::HighBasilica, 56, 96, 3},
        });

    std::cout << "-----------------------------------------------------------------------" << std::endl;
}

void runTests()
{
    size_t newMapTestPassCount = 0;
    size_t newMapTestFailCount = 0;
    size_t resizeMapTestPassCount = 0;
    size_t resizeMapTestFailCount = 0;
    size_t editMapTestPassCount = 0;
    size_t editMapTestFailCount = 0;
    auto mapDir = getTestMapDirectory();
    runNewMapTests(mapDir, newMapTestPassCount, newMapTestFailCount);
    runResizeMapTests(mapDir, resizeMapTestPassCount, resizeMapTestFailCount);
    runEditMapTests(mapDir, editMapTestPassCount, editMapTestFailCount);
    
    size_t totalNewMapTests = newMapTestPassCount + newMapTestFailCount;
    if ( newMapTestFailCount == 0 )
        std::cout << "PASS - New map tests - " << newMapTestPassCount << " / " << totalNewMapTests << std::endl;
    else
        std::cout << "FAIL - New map tests - " << newMapTestPassCount << " / " << totalNewMapTests << std::endl;

    size_t totalResizeMapTests = resizeMapTestPassCount + resizeMapTestFailCount;
    if ( resizeMapTestFailCount == 0 )
        std::cout << "PASS - Resize map tests - " << resizeMapTestPassCount << " / " << totalResizeMapTests << std::endl;
    else
        std::cout << "FAIL - Resize map tests - " << resizeMapTestPassCount << " / " << totalResizeMapTests << std::endl;

    size_t totalEditMapTests = editMapTestPassCount + editMapTestFailCount;
    if ( editMapTestFailCount == 0 )
        std::cout << "PASS - Edit map tests - " << editMapTestPassCount << " / " << totalEditMapTests << std::endl;
    else
        std::cout << "FAIL - Edit map tests - " << editMapTestPassCount << " / " << totalEditMapTests << std::endl;
}

namespace TestData
{
    inline constexpr uint16_t badlandsIsomLinks[] {
        0,0,0,0,0,0,0,0,0,0,0,0,0,2,1,1,1,1,1,1,1,1,1,1,1,1,3,4,4,2,4,4,2,4,4,2,4,4,2,5,5,5,3,5,5,3,5,5,3,5,5,3,6,2,2,5,2,2,5,2,2,5,2,2,5,14,10,10,9,10,10,
        9,10,10,9,10,10,9,15,11,11,10,11,11,10,11,11,10,11,11,10,7,3,3,6,3,3,6,3,3,6,3,3,6,18,14,14,7,14,14,7,14,14,7,14,14,7,4,15,15,4,15,15,4,15,15,4,15,
        15,4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,34,1,1,1,1,51,255,51,51,2,1,51,255,34,1,49,257,1,1,1,49,1,257,
        49,49,2,34,52,52,2,52,30,256,30,30,1,52,30,256,34,50,31,258,50,50,2,31,50,258,31,31,1,34,1,1,1,1,1,1,49,1,257,1,51,255,34,1,54,257,1,1,1,1,1,1,54,
        33,256,34,50,31,258,52,30,256,30,30,1,31,31,1,34,1,1,1,1,53,255,32,53,258,1,1,1,34,50,31,258,56,56,2,56,56,2,31,51,255,34,55,55,2,52,30,256,49,30,
        257,55,55,2,34,1,49,257,1,51,255,51,51,2,49,49,2,34,52,52,2,50,50,2,31,50,258,52,30,256,34,50,31,258,52,30,256,49,30,257,31,51,255,34,1,54,257,1,
        53,255,32,53,258,54,33,256,35,5,5,3,5,51,255,51,51,1,5,51,255,35,5,49,257,5,5,3,49,5,257,49,49,1,35,52,52,1,52,34,256,34,34,3,52,34,256,35,50,35,
        258,50,50,1,35,50,258,35,35,3,35,5,5,3,5,5,3,49,5,257,5,51,255,35,5,54,257,5,5,3,5,5,3,54,37,256,35,50,35,258,52,34,256,34,34,3,35,35,3,35,5,5,3,5,
        53,255,36,53,258,5,5,3,35,50,35,258,56,56,1,56,56,1,35,51,255,35,55,55,1,52,34,256,49,34,257,55,55,1,35,5,49,257,5,51,255,51,51,1,49,49,1,35,52,52,
        1,50,50,1,35,50,258,52,34,256,35,50,35,258,52,34,256,49,34,257,35,51,255,35,5,54,257,5,53,255,36,53,258,54,37,256,20,1,1,1,1,51,255,51,51,5,1,51,
        255,20,1,49,257,1,1,1,49,1,257,49,49,5,20,52,52,5,52,1,256,1,1,1,52,1,256,20,50,1,258,50,50,5,1,50,258,1,1,1,20,1,1,1,1,1,1,49,1,257,1,51,255,20,1,
        54,257,1,1,1,1,1,1,54,1,256,20,50,1,258,52,1,256,1,1,1,1,1,1,20,1,1,1,1,53,255,1,53,258,1,1,1,20,50,1,258,56,56,5,56,56,5,1,51,255,20,55,55,5,52,1,
        256,49,1,257,55,55,5,20,1,49,257,1,51,255,51,51,5,49,49,5,20,52,52,5,50,50,5,1,50,258,52,1,256,20,50,1,258,52,1,256,49,1,257,1,51,255,20,1,54,257,
        1,53,255,1,53,258,54,1,256,28,1,1,1,1,51,255,51,51,10,1,51,255,28,1,49,257,1,1,1,49,1,257,49,49,10,28,52,52,10,52,1,256,1,1,1,52,1,256,28,50,1,258,
        50,50,10,1,50,258,1,1,1,28,1,1,1,1,1,1,49,1,257,1,51,255,28,1,54,257,1,1,1,1,1,1,54,1,256,28,50,1,258,52,1,256,1,1,1,1,1,1,28,1,1,1,1,53,255,1,53,
        258,1,1,1,28,50,1,258,11,11,10,11,11,10,1,51,255,28,11,11,10,52,1,256,49,1,257,11,11,10,28,1,49,257,1,51,255,51,51,10,49,49,10,28,52,52,10,50,50,
        10,1,50,258,52,1,256,28,50,1,258,52,1,256,49,1,257,1,51,255,28,1,54,257,1,53,255,1,53,258,54,1,256,21,4,4,2,4,51,255,51,51,6,4,51,255,21,4,49,257,
        4,4,2,49,4,257,49,49,6,21,52,52,6,52,4,256,4,4,2,52,4,256,21,50,4,258,50,50,6,4,50,258,4,4,2,21,4,4,2,4,4,2,49,4,257,4,51,255,21,4,54,257,4,4,2,4,
        4,2,54,4,256,21,50,4,258,52,4,256,4,4,2,4,4,2,21,4,4,2,4,53,255,4,53,258,4,4,2,21,50,4,258,56,56,6,56,56,6,4,51,255,21,55,55,6,52,4,256,49,4,257,
        55,55,6,21,4,49,257,4,51,255,51,51,6,49,49,6,21,52,52,6,50,50,6,4,50,258,52,4,256,21,50,4,258,52,4,256,49,4,257,4,51,255,21,4,54,257,4,53,255,4,53,
        258,54,4,256,27,1,1,1,1,51,255,51,51,9,1,51,255,27,1,49,257,1,1,1,49,1,257,49,49,9,27,52,52,9,52,1,256,1,1,1,52,1,256,27,50,1,258,50,50,9,1,50,258,
        1,1,1,27,1,1,1,1,1,1,49,1,257,1,51,255,27,1,54,257,1,1,1,1,1,1,54,1,256,27,50,1,258,52,1,256,1,1,1,1,1,1,27,1,1,1,1,53,255,1,53,258,1,1,1,27,50,1,
        258,56,56,9,56,56,9,1,51,255,27,55,55,9,52,1,256,49,1,257,55,55,9,27,1,49,257,1,51,255,51,51,9,49,49,9,27,52,52,9,50,50,9,1,50,258,52,1,256,27,50,
        1,258,52,1,256,49,1,257,1,51,255,27,1,54,257,1,53,255,1,53,258,54,1,256,31,10,10,9,10,51,255,51,51,7,10,51,255,31,10,49,257,10,10,9,49,10,257,49,
        49,7,31,52,52,7,52,26,256,26,26,9,52,26,256,31,50,27,258,50,50,7,27,50,258,27,27,9,31,10,10,9,10,10,9,49,10,257,10,51,255,31,10,54,257,10,10,9,10,
        10,9,54,29,256,31,50,27,258,52,26,256,26,26,9,27,27,9,31,10,10,9,10,53,255,28,53,258,10,10,9,31,50,27,258,56,56,7,56,56,7,27,51,255,31,55,55,7,52,
        26,256,49,26,257,55,55,7,31,10,49,257,10,51,255,51,51,7,49,49,7,31,52,52,7,50,50,7,27,50,258,52,26,256,31,50,27,258,52,26,256,49,26,257,27,51,255,
        31,10,54,257,10,53,255,28,53,258,54,29,256,22,1,1,1,1,51,255,51,51,4,1,51,255,22,1,49,257,1,1,1,49,1,257,49,49,4,22,52,52,4,52,1,256,1,1,1,52,1,
        256,22,50,1,258,50,50,4,1,50,258,1,1,1,22,1,1,1,1,1,1,49,1,257,1,51,255,22,1,54,257,1,1,1,1,1,1,54,1,256,22,50,1,258,52,1,256,1,1,1,1,1,1,22,1,1,1,
        1,53,255,1,53,258,1,1,1,22,50,1,258,56,56,4,56,56,4,1,51,255,22,55,55,4,52,1,256,49,1,257,55,55,4,22,1,49,257,1,51,255,51,51,4,49,49,4,22,52,52,4,
        50,50,4,1,50,258,52,1,256,22,50,1,258,52,1,256,49,1,257,1,51,255,22,1,54,257,1,53,255,1,53,258,54,1,256
    };

    inline constexpr uint16_t spaceIsomLinks[] {
        0,0,0,0,0,0,0,0,0,0,0,0,0,2,1,1,1,1,1,1,1,1,1,1,1,1,3,2,2,3,2,2,3,2,2,3,2,2,3,0,0,0,0,0,0,0,0,0,0,0,0,0,5,4,4,5,4,4,5,4,4,5,4,4,5,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,7,6,6,7,6,6,7,6,6,7,6,6,7,8,13,13,8,13,13,8,13,13,8,13,13,8,9,18,18,9,18,18,9,18,18,
        9,18,18,9,4,3,3,4,3,3,4,3,3,4,3,3,4,6,5,5,6,5,5,6,5,5,6,5,5,6,10,8,8,10,8,8,10,8,8,10,8,8,10,11,7,7,2,7,7,2,7,7,2,7,7,2,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,20,1,1,1,1,51,255,51,51,3,1,51,255,20,1,49,257,1,1,1,49,1,257,49,49,3,20,52,52,3,52,
        23,256,23,23,1,52,23,256,20,50,24,258,50,50,3,24,50,258,24,24,1,20,1,1,1,1,1,1,49,1,257,1,51,255,20,1,54,257,1,1,1,1,1,1,54,26,256,20,50,24,258,52,
        23,256,23,23,1,24,24,1,20,1,1,1,1,53,255,25,53,258,1,1,1,20,50,24,258,56,56,3,56,56,3,24,51,255,20,55,55,3,52,23,256,49,23,257,55,55,3,20,1,49,257,
        1,51,255,51,51,3,49,49,3,20,52,52,3,50,50,3,24,50,258,52,23,256,20,50,24,258,52,23,256,49,23,257,24,51,255,20,1,54,257,1,53,255,25,53,258,54,26,
        256,21,2,2,3,2,51,255,51,51,5,2,51,255,21,2,49,257,2,2,3,49,2,257,49,49,5,21,52,52,5,52,27,256,27,27,3,52,27,256,21,50,28,258,50,50,5,28,50,258,28,
        28,3,21,2,2,3,2,2,3,49,2,257,2,51,255,21,2,54,257,2,2,3,2,2,3,54,30,256,21,50,28,258,52,27,256,27,27,3,28,28,3,21,2,2,3,2,53,255,29,53,258,2,2,3,
        21,50,28,258,56,56,5,56,56,5,28,51,255,21,55,55,5,52,27,256,49,27,257,55,55,5,21,2,49,257,2,51,255,51,51,5,49,49,5,21,52,52,5,50,50,5,28,50,258,52,
        27,256,21,50,28,258,52,27,256,49,27,257,28,51,255,21,2,54,257,2,53,255,29,53,258,54,30,256,16,2,2,3,2,51,255,51,51,7,2,51,255,16,2,49,257,2,2,3,49,
        2,257,49,49,7,16,52,52,7,52,2,256,2,2,3,52,2,256,16,50,2,258,50,50,7,2,50,258,2,2,3,16,2,2,3,2,2,3,49,2,257,2,51,255,16,2,54,257,2,2,3,2,2,3,54,2,
        256,16,50,2,258,52,2,256,2,2,3,2,2,3,16,2,2,3,2,53,255,2,53,258,2,2,3,16,50,2,258,56,56,7,56,56,7,2,51,255,16,55,55,7,52,2,256,49,2,257,55,55,7,16,
        2,49,257,2,51,255,51,51,7,49,49,7,16,52,52,7,50,50,7,2,50,258,52,2,256,16,50,2,258,52,2,256,49,2,257,2,51,255,16,2,54,257,2,53,255,2,53,258,54,2,
        256,17,13,13,8,13,51,255,51,51,3,13,51,255,17,13,49,257,13,13,8,49,13,257,49,49,3,17,52,52,3,52,14,256,14,14,8,52,14,256,17,50,15,258,50,50,3,15,
        50,258,15,15,8,17,13,13,8,13,13,8,49,13,257,13,51,255,17,13,54,257,13,13,8,13,13,8,54,17,256,17,50,15,258,52,14,256,14,14,8,15,15,8,17,13,13,8,13,
        53,255,16,53,258,13,13,8,17,50,15,258,56,56,3,56,56,3,15,51,255,17,55,55,3,52,14,256,49,14,257,55,55,3,17,13,49,257,13,51,255,51,51,3,49,49,3,17,
        52,52,3,50,50,3,15,50,258,52,14,256,17,50,15,258,52,14,256,49,14,257,15,51,255,17,13,54,257,13,53,255,16,53,258,54,17,256,18,18,18,9,18,51,255,51,
        51,3,18,51,255,18,18,49,257,18,18,9,49,18,257,49,49,3,18,52,52,3,52,19,256,19,19,9,52,19,256,18,50,20,258,50,50,3,20,50,258,20,20,9,18,18,18,9,18,
        18,9,49,18,257,18,51,255,18,18,54,257,18,18,9,18,18,9,54,22,256,18,50,20,258,52,19,256,19,19,9,20,20,9,18,18,18,9,18,53,255,21,53,258,18,18,9,18,
        50,20,258,56,56,3,56,56,3,20,51,255,18,55,55,3,52,19,256,49,19,257,55,55,3,18,18,49,257,18,51,255,51,51,3,49,49,3,18,52,52,3,50,50,3,20,50,258,52,
        19,256,18,50,20,258,52,19,256,49,19,257,20,51,255,18,18,54,257,18,53,255,21,53,258,54,22,256,14,2,2,3,2,51,255,51,51,4,2,51,255,14,2,49,257,2,2,3,
        49,2,257,49,49,4,14,52,52,4,52,2,256,2,2,3,52,2,256,14,50,2,258,50,50,4,2,50,258,2,2,3,14,2,2,3,2,2,3,49,2,257,2,51,255,14,2,54,257,2,2,3,2,2,3,54,
        2,256,14,50,2,258,52,2,256,2,2,3,2,2,3,14,2,2,3,2,53,255,2,53,258,2,2,3,14,50,2,258,56,56,4,56,56,4,2,51,255,14,55,55,4,52,2,256,49,2,257,55,55,4,
        14,2,49,257,2,51,255,51,51,4,49,49,4,14,52,52,4,50,50,4,2,50,258,52,2,256,14,50,2,258,52,2,256,49,2,257,2,51,255,14,2,54,257,2,53,255,2,53,258,54,
        2,256,15,4,4,5,4,51,255,51,51,6,4,51,255,15,4,49,257,4,4,5,49,4,257,49,49,6,15,52,52,6,52,4,256,4,4,5,52,4,256,15,50,4,258,50,50,6,4,50,258,4,4,5,
        15,4,4,5,4,4,5,49,4,257,4,51,255,15,4,54,257,4,4,5,4,4,5,54,4,256,15,50,4,258,52,4,256,4,4,5,4,4,5,15,4,4,5,4,53,255,4,53,258,4,4,5,15,50,4,258,56,
        56,6,56,56,6,4,51,255,15,55,55,6,52,4,256,49,4,257,55,55,6,15,4,49,257,4,51,255,51,51,6,49,49,6,15,52,52,6,50,50,6,4,50,258,52,4,256,15,50,4,258,
        52,4,256,49,4,257,4,51,255,15,4,54,257,4,53,255,4,53,258,54,4,256,19,2,2,3,2,51,255,51,51,10,2,51,255,19,2,49,257,2,2,3,49,2,257,49,49,10,19,52,52,
        10,52,9,256,9,9,3,52,9,256,19,50,10,258,50,50,10,10,50,258,10,10,3,19,2,2,3,2,2,3,49,2,257,2,51,255,19,2,54,257,2,2,3,2,2,3,54,12,256,19,50,10,258,
        52,9,256,9,9,3,10,10,3,19,2,2,3,2,53,255,11,53,258,2,2,3,19,50,10,258,56,56,10,56,56,10,10,51,255,19,55,55,10,52,9,256,49,9,257,55,55,10,19,2,49,
        257,2,51,255,51,51,10,49,49,10,19,52,52,10,50,50,10,10,50,258,52,9,256,19,50,10,258,52,9,256,49,9,257,10,51,255,19,2,54,257,2,53,255,11,53,258,54,
        12,256,13,2,2,3,2,51,255,51,51,2,2,51,255,13,2,49,257,2,2,3,49,2,257,49,49,2,13,52,52,2,52,2,256,2,2,3,52,2,256,13,50,2,258,50,50,2,2,50,258,2,2,3,
        13,2,2,3,2,2,3,49,2,257,2,51,255,13,2,54,257,2,2,3,2,2,3,54,2,256,13,50,2,258,52,2,256,2,2,3,2,2,3,13,2,2,3,2,53,255,2,53,258,2,2,3,13,50,2,258,56,
        56,2,56,56,2,2,51,255,13,55,55,2,52,2,256,49,2,257,55,55,2,13,2,49,257,2,51,255,51,51,2,49,49,2,13,52,52,2,50,50,2,2,50,258,52,2,256,13,50,2,258,
        52,2,256,49,2,257,2,51,255,13,2,54,257,2,53,255,2,53,258,54,2,256
    };

    inline constexpr uint16_t installationIsomLinks[] {
        0,0,0,0,0,0,0,0,0,0,0,0,0,2,1,1,1,1,1,1,1,1,1,1,1,1,3,2,2,2,2,2,2,2,2,2,2,2,2,6,3,3,3,3,3,3,3,3,3,3,3,3,4,4,4,4,4,4,4,4,4,4,4,4,4,5,5,5,5,5,5,5,5,
        5,5,5,5,5,8,7,7,6,7,7,6,7,7,6,7,7,6,7,6,6,7,6,6,7,6,6,7,6,6,7,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,12,1,1,1,1,
        51,255,51,51,2,1,51,255,12,1,49,257,1,1,1,49,1,257,49,49,2,12,52,52,2,52,8,256,8,8,1,52,8,256,12,50,9,258,50,50,2,9,50,258,9,9,1,12,1,1,1,1,1,1,49,
        1,257,1,51,255,12,1,54,257,1,1,1,1,1,1,54,11,256,12,50,9,258,52,8,256,8,8,1,9,9,1,12,1,1,1,1,53,255,10,53,258,1,1,1,12,50,9,258,56,56,2,56,56,2,9,
        51,255,12,55,55,2,52,8,256,49,8,257,55,55,2,12,1,49,257,1,51,255,51,51,2,49,49,2,12,52,52,2,50,50,2,9,50,258,52,8,256,12,50,9,258,52,8,256,49,8,
        257,9,51,255,12,1,54,257,1,53,255,10,53,258,54,11,256,13,2,2,2,2,51,255,51,51,3,2,51,255,13,2,49,257,2,2,2,49,2,257,49,49,3,13,52,52,3,52,12,256,
        12,12,2,52,12,256,13,50,13,258,50,50,3,13,50,258,13,13,2,13,2,2,2,2,2,2,49,2,257,2,51,255,13,2,54,257,2,2,2,2,2,2,54,15,256,13,50,13,258,52,12,256,
        12,12,2,13,13,2,13,2,2,2,2,53,255,14,53,258,2,2,2,13,50,13,258,56,56,3,56,56,3,13,51,255,13,55,55,3,52,12,256,49,12,257,55,55,3,13,2,49,257,2,51,
        255,51,51,3,49,49,3,13,52,52,3,50,50,3,13,50,258,52,12,256,13,50,13,258,52,12,256,49,12,257,13,51,255,13,2,54,257,2,53,255,14,53,258,54,15,256,10,
        1,1,1,1,51,255,51,51,4,1,51,255,10,1,49,257,1,1,1,49,1,257,49,49,4,10,52,52,4,52,1,256,1,1,1,52,1,256,10,50,1,258,50,50,4,1,50,258,1,1,1,10,1,1,1,
        1,1,1,49,1,257,1,51,255,10,1,54,257,1,1,1,1,1,1,54,1,256,10,50,1,258,52,1,256,1,1,1,1,1,1,10,1,1,1,1,53,255,1,53,258,1,1,1,10,50,1,258,56,56,4,56,
        56,4,1,51,255,10,55,55,4,52,1,256,49,1,257,55,55,4,10,1,49,257,1,51,255,51,51,4,49,49,4,10,52,52,4,50,50,4,1,50,258,52,1,256,10,50,1,258,52,1,256,
        49,1,257,1,51,255,10,1,54,257,1,53,255,1,53,258,54,1,256,11,2,2,2,2,51,255,51,51,5,2,51,255,11,2,49,257,2,2,2,49,2,257,49,49,5,11,52,52,5,52,2,256,
        2,2,2,52,2,256,11,50,2,258,50,50,5,2,50,258,2,2,2,11,2,2,2,2,2,2,49,2,257,2,51,255,11,2,54,257,2,2,2,2,2,2,54,2,256,11,50,2,258,52,2,256,2,2,2,2,2,
        2,11,2,2,2,2,53,255,2,53,258,2,2,2,11,50,2,258,56,56,5,56,56,5,2,51,255,11,55,55,5,52,2,256,49,2,257,55,55,5,11,2,49,257,2,51,255,51,51,5,49,49,5,
        11,52,52,5,50,50,5,2,50,258,52,2,256,11,50,2,258,52,2,256,49,2,257,2,51,255,11,2,54,257,2,53,255,2,53,258,54,2,256,14,1,1,1,1,51,255,51,51,6,1,51,
        255,14,1,49,257,1,1,1,49,1,257,49,49,6,14,52,52,6,52,1,256,1,1,1,52,1,256,14,50,1,258,50,50,6,1,50,258,1,1,1,14,1,1,1,1,1,1,49,1,257,1,51,255,14,1,
        54,257,1,1,1,1,1,1,54,1,256,14,50,1,258,52,1,256,1,1,1,1,1,1,14,1,1,1,1,53,255,1,53,258,1,1,1,14,50,1,258,56,56,6,56,56,6,1,51,255,14,55,55,6,52,1,
        256,49,1,257,55,55,6,14,1,49,257,1,51,255,51,51,6,49,49,6,14,52,52,6,50,50,6,1,50,258,52,1,256,14,50,1,258,52,1,256,49,1,257,1,51,255,14,1,54,257,
        1,53,255,1,53,258,54,1,256,15,6,6,7,6,51,255,51,51,1,6,51,255,15,6,49,257,6,6,7,49,6,257,49,49,1,15,52,52,1,52,16,256,16,16,7,52,16,256,15,50,17,
        258,50,50,1,17,50,258,17,17,7,15,6,6,7,6,6,7,49,6,257,6,51,255,15,6,54,257,6,6,7,6,6,7,54,19,256,15,50,17,258,52,16,256,16,16,7,17,17,7,15,6,6,7,6,
        53,255,18,53,258,6,6,7,15,50,17,258,56,56,1,56,56,1,17,51,255,15,55,55,1,52,16,256,49,16,257,55,55,1,15,6,49,257,6,51,255,51,51,1,49,49,1,15,52,52,
        1,50,50,1,17,50,258,52,16,256,15,50,17,258,52,16,256,49,16,257,17,51,255,15,6,54,257,6,53,255,18,53,258,54,19,256
    };

    inline constexpr uint16_t ashworldIsomLinks[] {
        0,0,0,0,0,0,0,0,0,0,0,0,0,8,7,7,1,7,7,1,7,7,1,7,7,1,2,1,1,2,1,1,2,1,1,2,1,1,2,3,2,2,3,2,2,3,2,2,3,2,2,3,6,5,5,4,5,5,4,5,5,4,5,5,4,4,3,3,5,3,3,5,3,
        3,5,3,3,5,5,4,4,6,4,4,6,4,4,6,4,4,6,7,6,6,7,6,6,7,6,6,7,6,6,7,9,8,8,8,8,8,8,8,8,8,8,8,8,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,17,7,7,1,7,51,255,51,51,2,7,
        51,255,17,7,49,257,7,7,1,49,7,257,49,49,2,17,52,52,2,52,9,256,9,9,1,52,9,256,17,50,10,258,50,50,2,10,50,258,10,10,1,17,7,7,1,7,7,1,49,7,257,7,51,
        255,17,7,54,257,7,7,1,7,7,1,54,12,256,17,50,10,258,52,9,256,9,9,1,10,10,1,17,7,7,1,7,53,255,11,53,258,7,7,1,17,50,10,258,56,56,2,56,56,2,10,51,255,
        17,55,55,2,52,9,256,49,9,257,55,55,2,17,7,49,257,7,51,255,51,51,2,49,49,2,17,52,52,2,50,50,2,10,50,258,52,9,256,17,50,10,258,52,9,256,49,9,257,10,
        51,255,17,7,54,257,7,53,255,11,53,258,54,12,256,16,1,1,2,1,51,255,51,51,5,1,51,255,16,1,49,257,1,1,2,49,1,257,49,49,5,16,52,52,5,52,13,256,13,13,2,
        52,13,256,16,50,14,258,50,50,5,14,50,258,14,14,2,16,1,1,2,1,1,2,49,1,257,1,51,255,16,1,54,257,1,1,2,1,1,2,54,16,256,16,50,14,258,52,13,256,13,13,2,
        14,14,2,16,1,1,2,1,53,255,15,53,258,1,1,2,16,50,14,258,56,56,5,56,56,5,14,51,255,16,55,55,5,52,13,256,49,13,257,55,55,5,16,1,49,257,1,51,255,51,51,
        5,49,49,5,16,52,52,5,50,50,5,14,50,258,52,13,256,16,50,14,258,52,13,256,49,13,257,14,51,255,16,1,54,257,1,53,255,15,53,258,54,16,256,11,1,1,2,1,51,
        255,51,51,3,1,51,255,11,1,49,257,1,1,2,49,1,257,49,49,3,11,52,52,3,52,1,256,1,1,2,52,1,256,11,50,1,258,50,50,3,1,50,258,1,1,2,11,1,1,2,1,1,2,49,1,
        257,1,51,255,11,1,54,257,1,1,2,1,1,2,54,1,256,11,50,1,258,52,1,256,1,1,2,1,1,2,11,1,1,2,1,53,255,1,53,258,1,1,2,11,50,1,258,56,56,3,56,56,3,1,51,
        255,11,55,55,3,52,1,256,49,1,257,55,55,3,11,1,49,257,1,51,255,51,51,3,49,49,3,11,52,52,3,50,50,3,1,50,258,52,1,256,11,50,1,258,52,1,256,49,1,257,1,
        51,255,11,1,54,257,1,53,255,1,53,258,54,1,256,12,3,3,5,3,51,255,51,51,6,3,51,255,12,3,49,257,3,3,5,49,3,257,49,49,6,12,52,52,6,52,3,256,3,3,5,52,3,
        256,12,50,3,258,50,50,6,3,50,258,3,3,5,12,3,3,5,3,3,5,49,3,257,3,51,255,12,3,54,257,3,3,5,3,3,5,54,3,256,12,50,3,258,52,3,256,3,3,5,3,3,5,12,3,3,5,
        3,53,255,3,53,258,3,3,5,12,50,3,258,56,56,6,56,56,6,3,51,255,12,55,55,6,52,3,256,49,3,257,55,55,6,12,3,49,257,3,51,255,51,51,6,49,49,6,12,52,52,6,
        50,50,6,3,50,258,52,3,256,12,50,3,258,52,3,256,49,3,257,3,51,255,12,3,54,257,3,53,255,3,53,258,54,3,256,13,1,1,2,1,51,255,51,51,4,1,51,255,13,1,49,
        257,1,1,2,49,1,257,49,49,4,13,52,52,4,52,1,256,1,1,2,52,1,256,13,50,1,258,50,50,4,1,50,258,1,1,2,13,1,1,2,1,1,2,49,1,257,1,51,255,13,1,54,257,1,1,
        2,1,1,2,54,1,256,13,50,1,258,52,1,256,1,1,2,1,1,2,13,1,1,2,1,53,255,1,53,258,1,1,2,13,50,1,258,5,5,4,5,5,4,1,51,255,13,5,5,4,52,1,256,49,1,257,5,5,
        4,13,1,49,257,1,51,255,51,51,4,49,49,4,13,52,52,4,50,50,4,1,50,258,52,1,256,13,50,1,258,52,1,256,49,1,257,1,51,255,13,1,54,257,1,53,255,1,53,258,
        54,1,256,14,3,3,5,3,51,255,51,51,7,3,51,255,14,3,49,257,3,3,5,49,3,257,49,49,7,14,52,52,7,52,3,256,3,3,5,52,3,256,14,50,3,258,50,50,7,3,50,258,3,3,
        5,14,3,3,5,3,3,5,49,3,257,3,51,255,14,3,54,257,3,3,5,3,3,5,54,3,256,14,50,3,258,52,3,256,3,3,5,3,3,5,14,3,3,5,3,53,255,3,53,258,3,3,5,14,50,3,258,
        6,6,7,6,6,7,3,51,255,14,6,6,7,52,3,256,49,3,257,6,6,7,14,3,49,257,3,51,255,51,51,7,49,49,7,14,52,52,7,50,50,7,3,50,258,52,3,256,14,50,3,258,52,3,
        256,49,3,257,3,51,255,14,3,54,257,3,53,255,3,53,258,54,3,256,15,1,1,2,1,51,255,51,51,8,1,51,255,15,1,49,257,1,1,2,49,1,257,49,49,8,15,52,52,8,52,1,
        256,1,1,2,52,1,256,15,50,1,258,50,50,8,1,50,258,1,1,2,15,1,1,2,1,1,2,49,1,257,1,51,255,15,1,54,257,1,1,2,1,1,2,54,1,256,15,50,1,258,52,1,256,1,1,2,
        1,1,2,15,1,1,2,1,53,255,1,53,258,1,1,2,15,50,1,258,56,56,8,56,56,8,1,51,255,15,55,55,8,52,1,256,49,1,257,55,55,8,15,1,49,257,1,51,255,51,51,8,49,
        49,8,15,52,52,8,50,50,8,1,50,258,52,1,256,15,50,1,258,52,1,256,49,1,257,1,51,255,15,1,54,257,1,53,255,1,53,258,54,1,256
    };

    inline constexpr uint16_t jungleIsomLinks[] {
        0,0,0,0,0,0,0,0,0,0,0,0,0,2,1,1,1,1,1,1,1,1,1,1,1,1,3,4,4,2,4,4,2,4,4,2,4,4,2,5,5,5,3,5,5,3,5,5,3,5,5,3,8,8,8,8,8,8,8,8,8,8,8,8,8,9,12,12,11,12,12,
        11,12,12,11,12,12,11,15,11,11,10,11,11,10,11,11,10,11,11,10,11,6,6,12,6,6,12,6,6,12,6,6,12,16,16,16,13,16,16,13,16,16,13,16,16,13,10,9,9,14,9,9,14,
        9,9,14,9,9,14,12,7,7,15,7,7,15,7,7,15,7,7,15,13,13,13,16,13,13,16,13,13,16,13,13,16,17,17,17,17,17,17,17,17,17,17,17,17,17,4,15,15,4,15,15,4,15,15,
        4,15,15,4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,34,1,1,1,1,51,255,51,51,2,1,51,255,34,1,49,257,1,1,1,49,1,
        257,49,49,2,34,52,52,2,52,30,256,30,30,1,52,30,256,34,50,31,258,50,50,2,31,50,258,31,31,1,34,1,1,1,1,1,1,49,1,257,1,51,255,34,1,54,257,1,1,1,1,1,1,
        54,33,256,34,50,31,258,52,30,256,30,30,1,31,31,1,34,1,1,1,1,53,255,32,53,258,1,1,1,34,50,31,258,56,56,2,56,56,2,31,51,255,34,55,55,2,52,30,256,49,
        30,257,55,55,2,34,1,49,257,1,51,255,51,51,2,49,49,2,34,52,52,2,50,50,2,31,50,258,52,30,256,34,50,31,258,52,30,256,49,30,257,31,51,255,34,1,54,257,
        1,53,255,32,53,258,54,33,256,35,5,5,3,5,51,255,51,51,1,5,51,255,35,5,49,257,5,5,3,49,5,257,49,49,1,35,52,52,1,52,34,256,34,34,3,52,34,256,35,50,35,
        258,50,50,1,35,50,258,35,35,3,35,5,5,3,5,5,3,49,5,257,5,51,255,35,5,54,257,5,5,3,5,5,3,54,37,256,35,50,35,258,52,34,256,34,34,3,35,35,3,35,5,5,3,5,
        53,255,36,53,258,5,5,3,35,50,35,258,56,56,1,56,56,1,35,51,255,35,55,55,1,52,34,256,49,34,257,55,55,1,35,5,49,257,5,51,255,51,51,1,49,49,1,35,52,52,
        1,50,50,1,35,50,258,52,34,256,35,50,35,258,52,34,256,49,34,257,35,51,255,35,5,54,257,5,53,255,36,53,258,54,37,256,23,1,1,1,1,51,255,51,51,8,1,51,
        255,23,1,49,257,1,1,1,49,1,257,49,49,8,23,52,52,8,52,1,256,1,1,1,52,1,256,23,50,1,258,50,50,8,1,50,258,1,1,1,23,1,1,1,1,1,1,49,1,257,1,51,255,23,1,
        54,257,1,1,1,1,1,1,54,1,256,23,50,1,258,52,1,256,1,1,1,1,1,1,23,1,1,1,1,53,255,1,53,258,1,1,1,23,50,1,258,56,56,8,56,56,8,1,51,255,23,55,55,8,52,1,
        256,49,1,257,55,55,8,23,1,49,257,1,51,255,51,51,8,49,49,8,23,52,52,8,50,50,8,1,50,258,52,1,256,23,50,1,258,52,1,256,49,1,257,1,51,255,23,1,54,257,
        1,53,255,1,53,258,54,1,256,28,1,1,1,1,51,255,51,51,10,1,51,255,28,1,49,257,1,1,1,49,1,257,49,49,10,28,52,52,10,52,1,256,1,1,1,52,1,256,28,50,1,258,
        50,50,10,1,50,258,1,1,1,28,1,1,1,1,1,1,49,1,257,1,51,255,28,1,54,257,1,1,1,1,1,1,54,1,256,28,50,1,258,52,1,256,1,1,1,1,1,1,28,1,1,1,1,53,255,1,53,
        258,1,1,1,28,50,1,258,11,11,10,11,11,10,1,51,255,28,11,11,10,52,1,256,49,1,257,11,11,10,28,1,49,257,1,51,255,51,51,10,49,49,10,28,52,52,10,50,50,
        10,1,50,258,52,1,256,28,50,1,258,52,1,256,49,1,257,1,51,255,28,1,54,257,1,53,255,1,53,258,54,1,256,29,8,8,8,8,51,255,51,51,11,8,51,255,29,8,49,257,
        8,8,8,49,8,257,49,49,11,29,52,52,11,52,8,256,8,8,8,52,8,256,29,50,8,258,50,50,11,8,50,258,8,8,8,29,8,8,8,8,8,8,49,8,257,8,51,255,29,8,54,257,8,8,8,
        8,8,8,54,8,256,29,50,8,258,52,8,256,8,8,8,8,8,8,29,8,8,8,8,53,255,8,53,258,8,8,8,29,50,8,258,12,12,11,12,12,11,8,51,255,29,12,12,11,52,8,256,49,8,
        257,12,12,11,29,8,49,257,8,51,255,51,51,11,49,49,11,29,52,52,11,50,50,11,8,50,258,52,8,256,29,50,8,258,52,8,256,49,8,257,8,51,255,29,8,54,257,8,53,
        255,8,53,258,54,8,256,25,8,8,8,8,51,255,51,51,12,8,51,255,25,8,49,257,8,8,8,49,8,257,49,49,12,25,52,52,12,52,8,256,8,8,8,52,8,256,25,50,8,258,50,
        50,12,8,50,258,8,8,8,25,8,8,8,8,8,8,49,8,257,8,51,255,25,8,54,257,8,8,8,8,8,8,54,8,256,25,50,8,258,52,8,256,8,8,8,8,8,8,25,8,8,8,8,53,255,8,53,258,
        8,8,8,25,50,8,258,6,6,12,6,6,12,8,51,255,25,6,6,12,52,8,256,49,8,257,6,6,12,25,8,49,257,8,51,255,51,51,12,49,49,12,25,52,52,12,50,50,12,8,50,258,
        52,8,256,25,50,8,258,52,8,256,49,8,257,8,51,255,25,8,54,257,8,53,255,8,53,258,54,8,256,32,8,8,8,8,51,255,51,51,13,8,51,255,32,8,49,257,8,8,8,49,8,
        257,49,49,13,32,52,52,13,52,18,256,18,18,8,52,18,256,32,50,19,258,50,50,13,19,50,258,19,19,8,32,8,8,8,8,8,8,49,8,257,8,51,255,32,8,54,257,8,8,8,8,
        8,8,54,21,256,32,50,19,258,52,18,256,18,18,8,19,19,8,32,8,8,8,8,53,255,20,53,258,8,8,8,32,50,19,258,56,56,13,56,56,13,19,51,255,32,55,55,13,52,18,
        256,49,18,257,55,55,13,32,8,49,257,8,51,255,51,51,13,49,49,13,32,52,52,13,50,50,13,19,50,258,52,18,256,32,50,19,258,52,18,256,49,18,257,19,51,255,
        32,8,54,257,8,53,255,20,53,258,54,21,256,24,4,4,2,4,51,255,51,51,14,4,51,255,24,4,49,257,4,4,2,49,4,257,49,49,14,24,52,52,14,52,4,256,4,4,2,52,4,
        256,24,50,4,258,50,50,14,4,50,258,4,4,2,24,4,4,2,4,4,2,49,4,257,4,51,255,24,4,54,257,4,4,2,4,4,2,54,4,256,24,50,4,258,52,4,256,4,4,2,4,4,2,24,4,4,
        2,4,53,255,4,53,258,4,4,2,24,50,4,258,56,56,14,56,56,14,4,51,255,24,55,55,14,52,4,256,49,4,257,55,55,14,24,4,49,257,4,51,255,51,51,14,49,49,14,24,
        52,52,14,50,50,14,4,50,258,52,4,256,24,50,4,258,52,4,256,49,4,257,4,51,255,24,4,54,257,4,53,255,4,53,258,54,4,256,26,9,9,14,9,51,255,51,51,15,9,51,
        255,26,9,49,257,9,9,14,49,9,257,49,49,15,26,52,52,15,52,9,256,9,9,14,52,9,256,26,50,9,258,50,50,15,9,50,258,9,9,14,26,9,9,14,9,9,14,49,9,257,9,51,
        255,26,9,54,257,9,9,14,9,9,14,54,9,256,26,50,9,258,52,9,256,9,9,14,9,9,14,26,9,9,14,9,53,255,9,53,258,9,9,14,26,50,9,258,7,7,15,7,7,15,9,51,255,26,
        7,7,15,52,9,256,49,9,257,7,7,15,26,9,49,257,9,51,255,51,51,15,49,49,15,26,52,52,15,50,50,15,9,50,258,52,9,256,26,50,9,258,52,9,256,49,9,257,9,51,
        255,26,9,54,257,9,53,255,9,53,258,54,9,256,30,9,9,14,9,51,255,51,51,16,9,51,255,30,9,49,257,9,9,14,49,9,257,49,49,16,30,52,52,16,52,9,256,9,9,14,
        52,9,256,30,50,9,258,50,50,16,9,50,258,9,9,14,30,9,9,14,9,9,14,49,9,257,9,51,255,30,9,54,257,9,9,14,9,9,14,54,9,256,30,50,9,258,52,9,256,9,9,14,9,
        9,14,30,9,9,14,9,53,255,9,53,258,9,9,14,30,50,9,258,13,13,16,13,13,16,9,51,255,30,13,13,16,52,9,256,49,9,257,13,13,16,30,9,49,257,9,51,255,51,51,
        16,49,49,16,30,52,52,16,50,50,16,9,50,258,52,9,256,30,50,9,258,52,9,256,49,9,257,9,51,255,30,9,54,257,9,53,255,9,53,258,54,9,256,33,9,9,14,9,51,
        255,51,51,17,9,51,255,33,9,49,257,9,9,14,49,9,257,49,49,17,33,52,52,17,52,22,256,22,22,14,52,22,256,33,50,23,258,50,50,17,23,50,258,23,23,14,33,9,
        9,14,9,9,14,49,9,257,9,51,255,33,9,54,257,9,9,14,9,9,14,54,25,256,33,50,23,258,52,22,256,22,22,14,23,23,14,33,9,9,14,9,53,255,24,53,258,9,9,14,33,
        50,23,258,56,56,17,56,56,17,23,51,255,33,55,55,17,52,22,256,49,22,257,55,55,17,33,9,49,257,9,51,255,51,51,17,49,49,17,33,52,52,17,50,50,17,23,50,
        258,52,22,256,33,50,23,258,52,22,256,49,22,257,23,51,255,33,9,54,257,9,53,255,24,53,258,54,25,256,22,1,1,1,1,51,255,51,51,4,1,51,255,22,1,49,257,1,
        1,1,49,1,257,49,49,4,22,52,52,4,52,1,256,1,1,1,52,1,256,22,50,1,258,50,50,4,1,50,258,1,1,1,22,1,1,1,1,1,1,49,1,257,1,51,255,22,1,54,257,1,1,1,1,1,
        1,54,1,256,22,50,1,258,52,1,256,1,1,1,1,1,1,22,1,1,1,1,53,255,1,53,258,1,1,1,22,50,1,258,56,56,4,56,56,4,1,51,255,22,55,55,4,52,1,256,49,1,257,55,
        55,4,22,1,49,257,1,51,255,51,51,4,49,49,4,22,52,52,4,50,50,4,1,50,258,52,1,256,22,50,1,258,52,1,256,49,1,257,1,51,255,22,1,54,257,1,53,255,1,53,
        258,54,1,256
    };
}

void linkTableGenTest()
{
    bool anyTilesetError = false;
    Span<uint16_t> isomDataTables[] { TestData::badlandsIsomLinks, TestData::spaceIsomLinks, TestData::installationIsomLinks, TestData::ashworldIsomLinks, TestData::jungleIsomLinks };
    for ( size_t tileset=0; tileset<5; ++tileset )
    {
        const auto & isomLinks = terrainDat.get(Sc::Terrain::Tileset(tileset)).isomLinks;

        std::cout << "-----------" << std::endl;
        size_t checkSize = isomDataTables[tileset].size()/13;

        // Print isomTable / perform error check
        bool error = false;
        for ( size_t i=0; i<checkSize; ++i )
        {
            const auto & entry = isomLinks[i];

            /*std::cout << " " << std::uppercase << std::setw(2) << ": "
                << std::setw(3) << int(entry.terrainType) << ", "
                << "{" << entry.topLeft.right << ", " << entry.topLeft.bottom << ", " << entry.topLeft.linkId << "}, "
                << "{" << entry.topRight.left << ", " << entry.topRight.bottom << ", " << entry.topRight.linkId << "}, "
                << "{" << entry.bottomRight.left << ", " << entry.bottomRight.top << ", " << entry.bottomRight.linkId << "}, "
                << "{" << entry.bottomLeft.top << ", " << entry.bottomLeft.right << ", " << entry.bottomLeft.linkId << "}"
                << std::endl;*/

            const auto & oldData = isomDataTables[tileset];
            auto start = 13*i;
            if ( entry.terrainType != oldData[start] ||
                entry.topLeft.right != Sc::Isom::Link(oldData[start+1]) ||
                entry.topLeft.bottom != Sc::Isom::Link(oldData[start+2]) ||
                entry.topLeft.linkId != Sc::Isom::LinkId(oldData[start+3]) ||
                entry.topRight.left != Sc::Isom::Link(oldData[start+4]) ||
                entry.topRight.bottom != Sc::Isom::Link(oldData[start+5]) ||
                entry.topRight.linkId != Sc::Isom::LinkId(oldData[start+6]) ||
                entry.bottomRight.left != Sc::Isom::Link(oldData[start+7]) ||
                entry.bottomRight.top != Sc::Isom::Link(oldData[start+8]) ||
                entry.bottomRight.linkId != Sc::Isom::LinkId(oldData[start+9]) ||
                entry.bottomLeft.top != Sc::Isom::Link(oldData[start+10]) ||
                entry.bottomLeft.right != Sc::Isom::Link(oldData[start+11]) ||
                entry.bottomLeft.linkId != Sc::Isom::LinkId(oldData[start+12])
                )
            {
                error = true;
            }
            
            if ( oldData[start+1] == 48 ||
                oldData[start+2] == 48 ||
                oldData[start+3] == 48 ||
                oldData[start+4] == 48 ||
                oldData[start+5] == 48 ||
                oldData[start+6] == 48 ||
                oldData[start+7] == 48 ||
                oldData[start+8] == 48 ||
                oldData[start+9] == 48 ||
                oldData[start+10] == 48 ||
                oldData[start+11] == 48 ||
                oldData[start+12] == 48 )
            {
                error = true;
            }
        }
        if ( error )
        {
            anyTilesetError = true;
            std::cout << "!!!!!!!!!!!!!!!!!!!!!!! Contains Errors" << std::endl;
        }
    }
    if ( anyTilesetError )
    {
        std::cout << "!!!!!!!!!!!!!!!!!!!!!!! Contains Errors" << std::endl;
    }
    else
        std::cout << "All looks perfect" << std::endl;
}

void testMain()
{
    std::string starcraftPath = "C:\\Program Files (x86)\\StarCraft";

    terrainDat.load(starcraftPath);

    runTests();
    
    linkTableGenTest();
}
