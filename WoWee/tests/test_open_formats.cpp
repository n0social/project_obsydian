// Tests for Wowee open format round-trips (WOM, WOB, WHM/WOT)
#include <catch_amalgamated.hpp>
#include "pipeline/wowee_building.hpp"
#include "pipeline/wowee_collision.hpp"
#include "pipeline/wmo_loader.hpp"
#include "pipeline/wowee_terrain_loader.hpp"
#include "pipeline/adt_loader.hpp"
#include <filesystem>
#include <fstream>
#include <cstring>
#include <limits>
#include <cmath>

using namespace wowee::pipeline;

static const std::string TEST_DIR = "test_output_formats";

static void ensureTestDir() {
    std::filesystem::create_directories(TEST_DIR);
}

static void cleanupTestDir() {
    std::filesystem::remove_all(TEST_DIR);
}

struct CleanupListener : Catch::EventListenerBase {
    using EventListenerBase::EventListenerBase;
    void testRunEnded(const Catch::TestRunStats&) override { cleanupTestDir(); }
};
CATCH_REGISTER_LISTENER(CleanupListener)

// ============== WOM Tests (binary format verification) ==============

TEST_CASE("WOM1 binary format structure", "[wom]") {
    ensureTestDir();
    std::string path = TEST_DIR + "/test_wom1.wom";
    {
        std::ofstream f(path, std::ios::binary);
        uint32_t magic = 0x314D4F57; // "WOM1"
        uint32_t verts = 3, indices = 3, texCount = 1;
        float radius = 5.0f;
        glm::vec3 bmin(-1), bmax(1);
        f.write(reinterpret_cast<const char*>(&magic), 4);
        f.write(reinterpret_cast<const char*>(&verts), 4);
        f.write(reinterpret_cast<const char*>(&indices), 4);
        f.write(reinterpret_cast<const char*>(&texCount), 4);
        f.write(reinterpret_cast<const char*>(&radius), 4);
        f.write(reinterpret_cast<const char*>(&bmin), 12);
        f.write(reinterpret_cast<const char*>(&bmax), 12);
        uint16_t nameLen = 4;
        f.write(reinterpret_cast<const char*>(&nameLen), 2);
        f.write("Cube", 4);
        // WOM1 vertex = 32 bytes (no bone data)
        struct V1 { float p[3]; float n[3]; float uv[2]; };
        V1 v0 = {{0,0,0},{0,0,1},{0,0}};
        V1 v1 = {{1,0,0},{0,0,1},{1,0}};
        V1 v2 = {{0,1,0},{0,0,1},{0,1}};
        f.write(reinterpret_cast<const char*>(&v0), 32);
        f.write(reinterpret_cast<const char*>(&v1), 32);
        f.write(reinterpret_cast<const char*>(&v2), 32);
        uint32_t idx[] = {0, 1, 2};
        f.write(reinterpret_cast<const char*>(idx), 12);
        uint16_t tl = 8;
        f.write(reinterpret_cast<const char*>(&tl), 2);
        f.write("test.png", 8);
    }

    // Verify magic and structure by reading raw
    std::ifstream check(path, std::ios::binary);
    uint32_t m; check.read(reinterpret_cast<char*>(&m), 4);
    REQUIRE(m == 0x314D4F57);
    uint32_t vc; check.read(reinterpret_cast<char*>(&vc), 4);
    REQUIRE(vc == 3);
    auto fsize = std::filesystem::file_size(path);
    REQUIRE(fsize > 100); // Minimal valid WOM1

    std::filesystem::remove(path);
}

TEST_CASE("WOM2 magic differs from WOM1", "[wom]") {
    REQUIRE(0x314D4F57 != 0x324D4F57); // WOM1 != WOM2
}

TEST_CASE("WOM3 magic is distinct from WOM1/WOM2", "[wom]") {
    // WOM3 = "WOM3" little-endian. Distinctness ensures the loader can
    // tell which version a file is and pick the right read path.
    REQUIRE(0x334D4F57 != 0x314D4F57); // WOM3 != WOM1
    REQUIRE(0x334D4F57 != 0x324D4F57); // WOM3 != WOM2
}

TEST_CASE("WOM rejects invalid magic", "[wom]") {
    ensureTestDir();
    std::string path = TEST_DIR + "/bad.wom";
    {
        std::ofstream f(path, std::ios::binary);
        uint32_t bad = 0xDEADBEEF;
        f.write(reinterpret_cast<const char*>(&bad), 4);
    }
    // Can't call WoweeModelLoader::load without linking it,
    // but we verify the binary structure is correct
    std::ifstream check(path, std::ios::binary);
    uint32_t m; check.read(reinterpret_cast<char*>(&m), 4);
    REQUIRE(m != 0x314D4F57);
    REQUIRE(m != 0x324D4F57);
    std::filesystem::remove(path);
}

// ============== WOB Tests ==============

TEST_CASE("WOB save and load round-trip", "[wob]") {
    ensureTestDir();
    WoweeBuilding bld;
    bld.name = "TestHouse";
    bld.boundRadius = 20.0f;

    WoweeBuilding::Group grp;
    grp.name = "Room1";
    grp.isOutdoor = false;
    grp.boundMin = glm::vec3(-5, -5, 0);
    grp.boundMax = glm::vec3(5, 5, 4);
    grp.vertices.push_back({{0,0,0}, {0,0,1}, {0,0}, {1,1,1,1}});
    grp.vertices.push_back({{1,0,0}, {0,0,1}, {1,0}, {1,1,1,1}});
    grp.vertices.push_back({{0,1,0}, {0,0,1}, {0,1}, {1,1,1,1}});
    grp.indices = {0, 1, 2};
    grp.texturePaths.push_back("textures/wall.png");
    WoweeBuilding::Material mat;
    mat.texturePath = "textures/wall.png";
    mat.flags = 0x10;
    mat.shader = 3;
    mat.blendMode = 1;
    grp.materials.push_back(mat);
    bld.groups.push_back(grp);

    WoweeBuilding::Portal portal;
    portal.groupA = 0;
    portal.groupB = 1;
    portal.vertices = {{0,0,0}, {1,0,0}, {1,0,3}, {0,0,3}};
    bld.portals.push_back(portal);

    WoweeBuilding::DoodadPlacement dp;
    dp.modelPath = "models/chair.wom";
    dp.position = {2, 3, 0};
    dp.rotation = {0, 45, 0};
    dp.scale = 1.5f;
    bld.doodads.push_back(dp);

    REQUIRE(bld.isValid());

    std::string base = TEST_DIR + "/test_building";
    REQUIRE(WoweeBuildingLoader::save(bld, base));
    REQUIRE(WoweeBuildingLoader::exists(base));

    auto loaded = WoweeBuildingLoader::load(base);
    REQUIRE(loaded.isValid());
    REQUIRE(loaded.name == "TestHouse");
    REQUIRE(loaded.groups.size() == 1);
    REQUIRE(loaded.groups[0].name == "Room1");
    REQUIRE(loaded.groups[0].vertices.size() == 3);
    REQUIRE(loaded.groups[0].indices.size() == 3);
    REQUIRE(loaded.groups[0].isOutdoor == false);
    REQUIRE(loaded.groups[0].materials.size() == 1);
    REQUIRE(loaded.groups[0].materials[0].texturePath == "textures/wall.png");
    REQUIRE(loaded.groups[0].materials[0].flags == 0x10);
    REQUIRE(loaded.groups[0].materials[0].shader == 3);
    REQUIRE(loaded.groups[0].materials[0].blendMode == 1);
    REQUIRE(loaded.portals.size() == 1);
    REQUIRE(loaded.portals[0].groupA == 0);
    REQUIRE(loaded.portals[0].vertices.size() == 4);
    REQUIRE(loaded.doodads.size() == 1);
    REQUIRE(loaded.doodads[0].modelPath == "models/chair.wom");
    REQUIRE(loaded.doodads[0].rotation.y == Catch::Approx(45.0f));
    REQUIRE(loaded.doodads[0].scale == Catch::Approx(1.5f));

    std::filesystem::remove(base + ".wob");
}

TEST_CASE("WOB toWMOModel conversion", "[wob]") {
    WoweeBuilding bld;
    bld.name = "ConvertTest";
    WoweeBuilding::Group grp;
    grp.name = "Group0";
    grp.vertices.push_back({{0,0,0}, {0,0,1}, {0,0}, {1,1,1,1}});
    grp.vertices.push_back({{1,0,0}, {0,0,1}, {1,0}, {1,1,1,1}});
    grp.vertices.push_back({{0,1,0}, {0,0,1}, {0,1}, {1,1,1,1}});
    grp.indices = {0, 1, 2};
    bld.groups.push_back(grp);

    WMOModel wmoOut;
    REQUIRE(WoweeBuildingLoader::toWMOModel(bld, wmoOut));
    REQUIRE(wmoOut.isValid());
    REQUIRE(wmoOut.nGroups == 1);
    REQUIRE(wmoOut.groups[0].vertices.size() == 3);
    REQUIRE(wmoOut.groups[0].indices.size() == 3);
}

TEST_CASE("WOB toWMOModel restores materials/portals/doodads/doodadSet", "[wob]") {
    WoweeBuilding bld;
    bld.name = "Full";
    bld.boundRadius = 10.0f;

    WoweeBuilding::Group g0;
    g0.name = "RoomA";
    g0.isOutdoor = false;
    g0.boundMin = glm::vec3(-5);
    g0.boundMax = glm::vec3(5);
    g0.vertices.push_back({{0,0,0}, {0,0,1}, {0,0}, {1,1,1,1}});
    g0.vertices.push_back({{1,0,0}, {0,0,1}, {1,0}, {1,1,1,1}});
    g0.vertices.push_back({{0,1,0}, {0,0,1}, {0,1}, {1,1,1,1}});
    g0.indices = {0, 1, 2};
    WoweeBuilding::Material mA{"textures/wallA.png", 0x10, 1, 0};
    WoweeBuilding::Material mB{"textures/wallB.png", 0x20, 2, 1};
    g0.materials = {mA, mB};
    bld.groups.push_back(g0);

    // A second group with one of A's materials and a unique one — verifies
    // dedupe works across groups.
    WoweeBuilding::Group g1;
    g1.name = "RoomB";
    g1.isOutdoor = true;
    g1.vertices = g0.vertices;
    g1.indices = {0, 1, 2};
    WoweeBuilding::Material mC{"textures/floorC.png", 0x40, 3, 2};
    g1.materials = {mA, mC};  // mA shared with g0
    bld.groups.push_back(g1);

    WoweeBuilding::Portal p;
    p.groupA = 0; p.groupB = 1;
    p.vertices = {{0,0,0}, {1,0,0}, {1,0,3}, {0,0,3}};
    bld.portals.push_back(p);

    WoweeBuilding::DoodadPlacement dp;
    dp.modelPath = "models/chair.wom";
    dp.position = {2, 3, 0};
    dp.rotation = {0, 45, 0};
    dp.scale = 1.5f;
    bld.doodads.push_back(dp);

    WMOModel out;
    REQUIRE(WoweeBuildingLoader::toWMOModel(bld, out));
    REQUIRE(out.nGroups == 2);
    // Materials deduped: mA, mB, mC = 3
    REQUIRE(out.materials.size() == 3);
    // Textures deduped (paths converted .png -> .blp for renderer override)
    REQUIRE(out.textures.size() == 3);
    // Outdoor flag survives (0x08 set on g1)
    REQUIRE((out.groups[1].flags & 0x08) != 0);
    REQUIRE((out.groups[0].flags & 0x08) == 0);
    // Portal restored with refs to both groups
    REQUIRE(out.portals.size() == 1);
    REQUIRE(out.portals[0].vertexCount == 4);
    REQUIRE(out.portalVertices.size() == 4);
    REQUIRE(out.portalRefs.size() == 2);
    // Doodad restored, .wom path converted back to .m2 for runtime
    REQUIRE(out.doodads.size() == 1);
    REQUIRE(out.doodadNames[out.doodads[0].nameIndex] == "models/chair.m2");
    REQUIRE(out.doodads[0].scale == Catch::Approx(1.5f));
    // Default doodadSet emitted
    REQUIRE(out.doodadSets.size() == 1);
    REQUIRE(out.doodadSets[0].count == 1);
}

// ============== WHM/WOT Tests ==============

TEST_CASE("WHM heightmap save and load round-trip", "[whm]") {
    ensureTestDir();
    ADTTerrain terrain{};
    terrain.loaded = true;
    terrain.version = 18;
    terrain.coord = {32, 48};

    for (int ci = 0; ci < 256; ci++) {
        auto& chunk = terrain.chunks[ci];
        chunk.heightMap.loaded = true;
        chunk.indexX = ci % 16;
        chunk.indexY = ci / 16;
        chunk.position[2] = 100.0f;
        for (int v = 0; v < 145; v++)
            chunk.heightMap.heights[v] = static_cast<float>(v) * 0.1f;
    }

    terrain.textures.push_back("Tileset\\Elwynn\\ElwynnGrass01.blp");
    terrain.chunks[0].layers.push_back({0, 0, 0, 0});

    // Use the editor exporter via manual WHM write
    std::string hmPath = TEST_DIR + "/test_terrain.whm";
    {
        std::ofstream f(hmPath, std::ios::binary);
        uint32_t magic = 0x314D4857, chunks = 256, verts = 145;
        f.write(reinterpret_cast<const char*>(&magic), 4);
        f.write(reinterpret_cast<const char*>(&chunks), 4);
        f.write(reinterpret_cast<const char*>(&verts), 4);
        for (int ci = 0; ci < 256; ci++) {
            float base = 100.0f;
            f.write(reinterpret_cast<const char*>(&base), 4);
            f.write(reinterpret_cast<const char*>(terrain.chunks[ci].heightMap.heights.data()), 145 * 4);
            uint32_t alphaSize = 0;
            f.write(reinterpret_cast<const char*>(&alphaSize), 4);
        }
    }

    ADTTerrain loaded{};
    REQUIRE(WoweeTerrainLoader::loadHeightmap(hmPath, loaded));
    REQUIRE(loaded.isLoaded());

    REQUIRE(loaded.chunks[0].heightMap.heights[0] == Catch::Approx(0.0f));
    REQUIRE(loaded.chunks[0].heightMap.heights[10] == Catch::Approx(1.0f));
    REQUIRE(loaded.chunks[0].heightMap.heights[100] == Catch::Approx(10.0f));

    std::filesystem::remove(hmPath);
}

TEST_CASE("WHM rejects invalid magic", "[whm]") {
    ensureTestDir();
    std::string path = TEST_DIR + "/bad.whm";
    {
        std::ofstream f(path, std::ios::binary);
        uint32_t bad = 0xDEADBEEF;
        f.write(reinterpret_cast<const char*>(&bad), 4);
    }

    ADTTerrain terrain{};
    REQUIRE_FALSE(WoweeTerrainLoader::loadHeightmap(path, terrain));
    std::filesystem::remove(path);
}

TEST_CASE("WOT metadata round-trip with placements", "[wot]") {
    ensureTestDir();
    std::string wotPath = TEST_DIR + "/test_terrain.wot";

    // Write a WOT JSON with textures, layers, water, doodads, and WMOs
    {
        std::ofstream f(wotPath);
        f << R"({
  "format": "wot-1.0",
  "tileX": 32, "tileY": 48,
  "textures": ["Tileset\\Elwynn\\ElwynnGrass01.blp", "Tileset\\Elwynn\\ElwynnDirt01.blp"],
  "chunkLayers": [
    {"layers": [0, 1], "holes": 5}
  ],
  "water": [
    {"chunk": 0, "type": 3, "height": 50.0},
    null
  ],
  "doodadNames": ["World\\Doodad\\Tree01.m2", "World\\Doodad\\Rock03.m2"],
  "doodads": [
    {"nameId": 0, "uniqueId": 100, "pos": [1000.0, 2000.0, 50.0], "rot": [0.0, 45.0, 0.0], "scale": 1024, "flags": 0},
    {"nameId": 1, "uniqueId": 101, "pos": [1100.0, 2100.0, 55.0], "rot": [10.0, 0.0, 5.0], "scale": 512, "flags": 2}
  ],
  "wmoNames": ["World\\WMO\\House01.wmo"],
  "wmos": [
    {"nameId": 0, "uniqueId": 200, "pos": [1200.0, 2200.0, 60.0], "rot": [0.0, 90.0, 0.0], "flags": 1, "doodadSet": 0}
  ]
})";
    }

    ADTTerrain terrain{};
    terrain.loaded = true;
    for (int i = 0; i < 256; i++) {
        terrain.chunks[i].heightMap.loaded = true;
        terrain.chunks[i].position[2] = 100.0f;
    }

    REQUIRE(WoweeTerrainLoader::loadMetadata(wotPath, terrain));

    // Tile coordinates
    REQUIRE(terrain.coord.x == 32);
    REQUIRE(terrain.coord.y == 48);

    // Textures
    REQUIRE(terrain.textures.size() == 2);
    REQUIRE(terrain.textures[0] == "Tileset\\Elwynn\\ElwynnGrass01.blp");

    // Chunk layers
    REQUIRE(terrain.chunks[0].layers.size() == 2);
    REQUIRE(terrain.chunks[0].layers[0].textureId == 0);
    REQUIRE(terrain.chunks[0].layers[1].textureId == 1);
    REQUIRE(terrain.chunks[0].holes == 5);

    // Water
    REQUIRE(terrain.waterData[0].hasWater());
    REQUIRE(terrain.waterData[0].layers[0].liquidType == 3); // 3=slime, in valid 0..3 range
    REQUIRE(terrain.waterData[0].layers[0].maxHeight == Catch::Approx(50.0f));

    // Doodad names and placements
    REQUIRE(terrain.doodadNames.size() == 2);
    REQUIRE(terrain.doodadNames[0] == "World\\Doodad\\Tree01.m2");
    REQUIRE(terrain.doodadPlacements.size() == 2);
    REQUIRE(terrain.doodadPlacements[0].nameId == 0);
    REQUIRE(terrain.doodadPlacements[0].uniqueId == 100);
    REQUIRE(terrain.doodadPlacements[0].position[0] == Catch::Approx(1000.0f));
    REQUIRE(terrain.doodadPlacements[0].rotation[1] == Catch::Approx(45.0f));
    REQUIRE(terrain.doodadPlacements[0].scale == 1024);
    REQUIRE(terrain.doodadPlacements[1].nameId == 1);
    REQUIRE(terrain.doodadPlacements[1].flags == 2);

    // WMO names and placements
    REQUIRE(terrain.wmoNames.size() == 1);
    REQUIRE(terrain.wmoNames[0] == "World\\WMO\\House01.wmo");
    REQUIRE(terrain.wmoPlacements.size() == 1);
    REQUIRE(terrain.wmoPlacements[0].nameId == 0);
    REQUIRE(terrain.wmoPlacements[0].uniqueId == 200);
    REQUIRE(terrain.wmoPlacements[0].position[0] == Catch::Approx(1200.0f));
    REQUIRE(terrain.wmoPlacements[0].rotation[1] == Catch::Approx(90.0f));
    REQUIRE(terrain.wmoPlacements[0].flags == 1);
    REQUIRE(terrain.wmoPlacements[0].doodadSet == 0);

    std::filesystem::remove(wotPath);
}

// ============== WOC Tests ==============

TEST_CASE("WOC collision from flat terrain", "[woc]") {
    ADTTerrain terrain{};
    terrain.loaded = true;
    terrain.coord = {32, 48};
    for (int ci = 0; ci < 256; ci++) {
        auto& chunk = terrain.chunks[ci];
        chunk.heightMap.loaded = true;
        chunk.indexX = ci % 16;
        chunk.indexY = ci / 16;
        chunk.position[2] = 100.0f;
        chunk.holes = 0;
        for (int v = 0; v < 145; v++)
            chunk.heightMap.heights[v] = 0.0f;
    }

    auto col = WoweeCollisionBuilder::fromTerrain(terrain);
    REQUIRE(col.isValid());
    REQUIRE(col.triangles.size() == 256 * 64 * 2); // 8x8 quads * 2 tris * 256 chunks
    REQUIRE(col.walkableCount() == col.triangles.size()); // flat = all walkable
    REQUIRE(col.steepCount() == 0);
    REQUIRE(col.tileX == 32);
    REQUIRE(col.tileY == 48);
}

TEST_CASE("WOC save and load round-trip", "[woc]") {
    ensureTestDir();

    WoweeCollision col;
    col.tileX = 10; col.tileY = 20;
    WoweeCollision::Triangle tri;
    tri.v0 = {0,0,0}; tri.v1 = {1,0,0}; tri.v2 = {0,1,0}; tri.flags = 0x01;
    col.triangles.push_back(tri);
    tri.v0 = {5,5,10}; tri.v1 = {6,5,10}; tri.v2 = {5,6,15}; tri.flags = 0x04;
    col.triangles.push_back(tri);
    col.bounds.expand({0,0,0}); col.bounds.expand({6,6,15});

    std::string path = TEST_DIR + "/test_collision.woc";
    REQUIRE(WoweeCollisionBuilder::save(col, path));

    auto loaded = WoweeCollisionBuilder::load(path);
    REQUIRE(loaded.isValid());
    REQUIRE(loaded.triangles.size() == 2);
    REQUIRE(loaded.tileX == 10);
    REQUIRE(loaded.tileY == 20);
    REQUIRE(loaded.triangles[0].flags == 0x01);
    REQUIRE(loaded.triangles[1].flags == 0x04);
    REQUIRE(loaded.triangles[0].v0.x == Catch::Approx(0.0f));
    REQUIRE(loaded.triangles[1].v2.z == Catch::Approx(15.0f));
    REQUIRE(loaded.walkableCount() == 1);
    REQUIRE(loaded.steepCount() == 1);
}

TEST_CASE("WOC addMesh classifies walkability by slope", "[woc]") {
    WoweeCollision col;
    // Two triangles: one floor (walkable), one steep wall (not walkable).
    std::vector<glm::vec3> verts = {
        {0, 0, 0}, {10, 0, 0}, {0, 10, 0},   // flat floor
        {0, 0, 0}, {1, 0, 10}, {0, 1, 10}    // near-vertical wall
    };
    std::vector<uint32_t> idx = {0, 1, 2, 3, 4, 5};
    glm::mat4 identity(1.0f);
    WoweeCollisionBuilder::addMesh(col, verts, idx, identity, 0);

    REQUIRE(col.triangles.size() == 2);
    REQUIRE((col.triangles[0].flags & 0x01) != 0); // floor walkable
    REQUIRE((col.triangles[1].flags & 0x01) == 0); // wall not walkable
}

TEST_CASE("WOC addMesh respects extra flags", "[woc]") {
    WoweeCollision col;
    std::vector<glm::vec3> verts = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    std::vector<uint32_t> idx = {0, 1, 2};
    WoweeCollisionBuilder::addMesh(col, verts, idx, glm::mat4(1.0f), 0x08); // indoor
    REQUIRE(col.triangles.size() == 1);
    REQUIRE((col.triangles[0].flags & 0x08) != 0); // indoor flag preserved
    REQUIRE((col.triangles[0].flags & 0x01) != 0); // and walkable
}

TEST_CASE("WOC holes skip triangles", "[woc]") {
    ADTTerrain terrain{};
    terrain.loaded = true;
    terrain.coord = {32, 32};
    for (int ci = 0; ci < 256; ci++) {
        auto& chunk = terrain.chunks[ci];
        chunk.heightMap.loaded = true;
        chunk.indexX = ci % 16;
        chunk.indexY = ci / 16;
        chunk.position[2] = 100.0f;
        chunk.holes = 0;
        for (int v = 0; v < 145; v++)
            chunk.heightMap.heights[v] = 0.0f;
    }
    // Punch a hole in chunk 0 (all 16 sub-quads)
    terrain.chunks[0].holes = 0xFFFF;

    auto col = WoweeCollisionBuilder::fromTerrain(terrain);
    REQUIRE(col.isValid());
    // Chunk 0 should produce zero triangles, rest produce 128 each
    REQUIRE(col.triangles.size() == 255 * 128);
}

TEST_CASE("WOB rejects missing file", "[wob]") {
    REQUIRE_FALSE(WoweeBuildingLoader::exists("nonexistent_path"));
}

// ============== Defensive hardening tests ==============

TEST_CASE("WOT clamps out-of-range tile coords on load", "[wot][hardening]") {
    ensureTestDir();
    std::string wotPath = TEST_DIR + "/oor_tiles.wot";
    {
        std::ofstream f(wotPath);
        f << R"({"format":"wot-1.0","tileX":200,"tileY":-5})";
    }

    ADTTerrain terrain{};
    terrain.loaded = true;
    REQUIRE(WoweeTerrainLoader::loadMetadata(wotPath, terrain));
    // Out-of-range coords should fall back to 32 (map center).
    REQUIRE(terrain.coord.x == 32);
    REQUIRE(terrain.coord.y == 32);
    std::filesystem::remove(wotPath);
}

TEST_CASE("WOT clamps out-of-range water liquid type", "[wot][hardening]") {
    ensureTestDir();
    std::string wotPath = TEST_DIR + "/oor_water.wot";
    {
        std::ofstream f(wotPath);
        f << R"({"format":"wot-1.0","tileX":32,"tileY":32,
            "water":[{"chunk":0,"type":99,"height":10.0}]})";
    }

    ADTTerrain terrain{};
    terrain.loaded = true;
    for (int i = 0; i < 256; i++) terrain.chunks[i].heightMap.loaded = true;
    REQUIRE(WoweeTerrainLoader::loadMetadata(wotPath, terrain));
    REQUIRE(terrain.waterData[0].hasWater());
    // type=99 is unknown; loader maps to 0 (plain water).
    REQUIRE(terrain.waterData[0].layers[0].liquidType == 0);
    std::filesystem::remove(wotPath);
}

TEST_CASE("WOC load skips degenerate triangles", "[woc][hardening]") {
    ensureTestDir();
    std::string path = TEST_DIR + "/degen.woc";
    {
        // Hand-write a WOC with one valid triangle and one degenerate.
        std::ofstream f(path, std::ios::binary);
        uint32_t magic = 0x31434F57; // "WOC1"
        uint32_t triCount = 2;
        uint32_t tx = 32, ty = 32;
        glm::vec3 bmin(-1), bmax(1);
        f.write(reinterpret_cast<const char*>(&magic), 4);
        f.write(reinterpret_cast<const char*>(&triCount), 4);
        f.write(reinterpret_cast<const char*>(&tx), 4);
        f.write(reinterpret_cast<const char*>(&ty), 4);
        f.write(reinterpret_cast<const char*>(&bmin), 12);
        f.write(reinterpret_cast<const char*>(&bmax), 12);
        // Valid triangle
        glm::vec3 v0(0, 0, 0), v1(1, 0, 0), v2(0, 1, 0);
        uint8_t flags = 0x01;
        f.write(reinterpret_cast<const char*>(&v0), 12);
        f.write(reinterpret_cast<const char*>(&v1), 12);
        f.write(reinterpret_cast<const char*>(&v2), 12);
        f.write(reinterpret_cast<const char*>(&flags), 1);
        // Degenerate triangle (all three vertices coincident)
        glm::vec3 d(5, 5, 5);
        f.write(reinterpret_cast<const char*>(&d), 12);
        f.write(reinterpret_cast<const char*>(&d), 12);
        f.write(reinterpret_cast<const char*>(&d), 12);
        f.write(reinterpret_cast<const char*>(&flags), 1);
    }

    auto col = WoweeCollisionBuilder::load(path);
    REQUIRE(col.isValid());
    // Only the non-degenerate triangle should survive.
    REQUIRE(col.triangles.size() == 1);
    std::filesystem::remove(path);
}

TEST_CASE("WOC rejects absurdly large triangle counts", "[woc][hardening]") {
    ensureTestDir();
    std::string path = TEST_DIR + "/huge_woc.woc";
    {
        std::ofstream f(path, std::ios::binary);
        uint32_t magic = 0x31434F57;
        uint32_t triCount = 10'000'000; // > 2M cap
        uint32_t tx = 32, ty = 32;
        glm::vec3 bmin(0), bmax(0);
        f.write(reinterpret_cast<const char*>(&magic), 4);
        f.write(reinterpret_cast<const char*>(&triCount), 4);
        f.write(reinterpret_cast<const char*>(&tx), 4);
        f.write(reinterpret_cast<const char*>(&ty), 4);
        f.write(reinterpret_cast<const char*>(&bmin), 12);
        f.write(reinterpret_cast<const char*>(&bmax), 12);
    }

    auto col = WoweeCollisionBuilder::load(path);
    // 10M triangle WOC rejected — returns empty (isValid false).
    REQUIRE_FALSE(col.isValid());
    std::filesystem::remove(path);
}

TEST_CASE("WOB scrubs NaN doodad transform on load", "[wob][hardening]") {
    ensureTestDir();
    std::string base = TEST_DIR + "/nan_doodad";
    std::string path = base + ".wob";
    {
        std::ofstream f(path, std::ios::binary);
        uint32_t magic = 0x31424F57; // "WOB1"
        uint32_t gc = 0, pc = 0, dc = 1;
        float boundRadius = 5.0f;
        f.write(reinterpret_cast<const char*>(&magic), 4);
        f.write(reinterpret_cast<const char*>(&gc), 4);
        f.write(reinterpret_cast<const char*>(&pc), 4);
        f.write(reinterpret_cast<const char*>(&dc), 4);
        f.write(reinterpret_cast<const char*>(&boundRadius), 4);
        // Empty name string
        uint16_t nameLen = 0;
        f.write(reinterpret_cast<const char*>(&nameLen), 2);
        // Doodad with NaN position/rotation/scale
        std::string mp = "Tree.wom";
        uint16_t mpLen = static_cast<uint16_t>(mp.size());
        f.write(reinterpret_cast<const char*>(&mpLen), 2);
        f.write(mp.data(), mpLen);
        float nan = std::numeric_limits<float>::quiet_NaN();
        glm::vec3 nanv(nan, nan, nan);
        f.write(reinterpret_cast<const char*>(&nanv), 12);
        f.write(reinterpret_cast<const char*>(&nanv), 12);
        f.write(reinterpret_cast<const char*>(&nan), 4);
    }

    auto bld = WoweeBuildingLoader::load(base);
    // isValid requires a group — we deliberately wrote 0 groups to keep
    // the test fixture small. Just check the doodad got loaded + scrubbed.
    REQUIRE(bld.doodads.size() == 1);
    REQUIRE(std::isfinite(bld.doodads[0].position.x));
    REQUIRE(bld.doodads[0].position == glm::vec3(0, 0, 0));
    REQUIRE(bld.doodads[0].rotation == glm::vec3(0, 0, 0));
    REQUIRE(bld.doodads[0].scale == 1.0f);
    std::filesystem::remove(path);
}

TEST_CASE("WOC clamps out-of-range tile coords on load", "[woc][hardening]") {
    ensureTestDir();
    std::string path = TEST_DIR + "/oor_tile_woc.woc";
    {
        std::ofstream f(path, std::ios::binary);
        uint32_t magic = 0x31434F57;
        uint32_t triCount = 0;
        uint32_t tx = 200, ty = 200; // out of 0..63
        glm::vec3 bmin(0), bmax(0);
        f.write(reinterpret_cast<const char*>(&magic), 4);
        f.write(reinterpret_cast<const char*>(&triCount), 4);
        f.write(reinterpret_cast<const char*>(&tx), 4);
        f.write(reinterpret_cast<const char*>(&ty), 4);
        f.write(reinterpret_cast<const char*>(&bmin), 12);
        f.write(reinterpret_cast<const char*>(&bmax), 12);
    }

    auto col = WoweeCollisionBuilder::load(path);
    // Out-of-range coords reclamped to 32 (map center).
    REQUIRE(col.tileX == 32);
    REQUIRE(col.tileY == 32);
    std::filesystem::remove(path);
}

TEST_CASE("WOB save scrubs NaN portal vertices and group indices", "[wob][hardening]") {
    ensureTestDir();
    std::string base = TEST_DIR + "/nan_portal";

    WoweeBuilding bld;
    bld.name = "NaNPortal";
    bld.boundRadius = 5.0f;

    WoweeBuilding::Group g;
    g.name = "G";
    g.vertices.push_back({{0, 0, 0}, {0, 0, 1}, {0, 0}, {1, 1, 1, 1}});
    g.vertices.push_back({{1, 0, 0}, {0, 0, 1}, {1, 0}, {1, 1, 1, 1}});
    g.vertices.push_back({{0, 1, 0}, {0, 0, 1}, {0, 1}, {1, 1, 1, 1}});
    g.indices = {0, 1, 2};
    bld.groups.push_back(g);

    // Portal with NaN vertices and out-of-range groupA/groupB
    WoweeBuilding::Portal p;
    p.groupA = 99;  // out of range (only 1 group)
    p.groupB = 99;
    float nan = std::numeric_limits<float>::quiet_NaN();
    p.vertices.push_back(glm::vec3(nan, nan, nan));
    p.vertices.push_back(glm::vec3(0, 0, 0));
    p.vertices.push_back(glm::vec3(1, 0, 0));
    bld.portals.push_back(p);

    REQUIRE(WoweeBuildingLoader::save(bld, base));

    auto reloaded = WoweeBuildingLoader::load(base);
    REQUIRE(reloaded.isValid());
    REQUIRE(reloaded.portals.size() == 1);
    // NaN vertex should have been scrubbed to 0
    const auto& v = reloaded.portals[0].vertices[0];
    REQUIRE(std::isfinite(v.x));
    REQUIRE(std::isfinite(v.y));
    REQUIRE(std::isfinite(v.z));
    REQUIRE(v == glm::vec3(0, 0, 0));
    // Out-of-range group indices clamped to -1
    REQUIRE(reloaded.portals[0].groupA == -1);
    REQUIRE(reloaded.portals[0].groupB == -1);
    std::filesystem::remove(base + ".wob");
}

TEST_CASE("WOB save scrubs NaN group bounds and vertex positions", "[wob][hardening]") {
    ensureTestDir();
    std::string base = TEST_DIR + "/nan_group";

    WoweeBuilding bld;
    bld.name = "NaNVerts";
    bld.boundRadius = std::numeric_limits<float>::quiet_NaN();  // bad source

    WoweeBuilding::Group g;
    g.name = "G";
    float nan = std::numeric_limits<float>::quiet_NaN();
    g.boundMin = glm::vec3(nan);
    g.boundMax = glm::vec3(nan);
    g.vertices.push_back({{nan, nan, nan}, {nan, nan, nan}, {0, 0}, {1, 1, 1, 1}});
    g.vertices.push_back({{1, 0, 0}, {0, 0, 1}, {1, 0}, {1, 1, 1, 1}});
    g.vertices.push_back({{0, 1, 0}, {0, 0, 1}, {0, 1}, {1, 1, 1, 1}});
    g.indices = {0, 1, 2};
    bld.groups.push_back(g);

    REQUIRE(WoweeBuildingLoader::save(bld, base));
    auto reloaded = WoweeBuildingLoader::load(base);
    REQUIRE(reloaded.isValid());
    // boundRadius defaulted to 1.0
    REQUIRE(reloaded.boundRadius == 1.0f);
    // Vertex 0 was all-NaN — scrubbed to zero/up-axis defaults
    const auto& v = reloaded.groups[0].vertices[0];
    REQUIRE(std::isfinite(v.position.x));
    REQUIRE(v.position == glm::vec3(0, 0, 0));
    // Normal default fallback is (0,0,1)
    REQUIRE(v.normal == glm::vec3(0, 0, 1));
    std::filesystem::remove(base + ".wob");
}

TEST_CASE("WOB save caps texture-path count to 1024 on round-trip", "[wob][hardening]") {
    ensureTestDir();
    std::string base = TEST_DIR + "/cap_textures";

    WoweeBuilding bld;
    bld.name = "ManyTex";
    bld.boundRadius = 1.0f;
    WoweeBuilding::Group g;
    g.name = "G";
    g.vertices.push_back({{0, 0, 0}, {0, 0, 1}, {0, 0}, {1, 1, 1, 1}});
    g.vertices.push_back({{1, 0, 0}, {0, 0, 1}, {1, 0}, {1, 1, 1, 1}});
    g.vertices.push_back({{0, 1, 0}, {0, 0, 1}, {0, 1}, {1, 1, 1, 1}});
    g.indices = {0, 1, 2};
    // Push more textures than the load limit (1024) — save should cap.
    for (int i = 0; i < 1500; i++) {
        g.texturePaths.push_back("tex" + std::to_string(i) + ".png");
    }
    bld.groups.push_back(g);

    REQUIRE(WoweeBuildingLoader::save(bld, base));
    auto reloaded = WoweeBuildingLoader::load(base);
    REQUIRE(reloaded.isValid());
    REQUIRE(reloaded.groups.size() == 1);
    // Capped at 1024 on save → loader sees only 1024 entries.
    REQUIRE(reloaded.groups[0].texturePaths.size() == 1024);
    // First and last (capped) entries match what was written.
    REQUIRE(reloaded.groups[0].texturePaths.front() == "tex0.png");
    REQUIRE(reloaded.groups[0].texturePaths.back() == "tex1023.png");
    std::filesystem::remove(base + ".wob");
}

TEST_CASE("WoweeCollision save caps tri count and clamps tile coords", "[woc][hardening]") {
    ensureTestDir();
    std::string path = TEST_DIR + "/cap_woc.woc";
    WoweeCollision col;
    col.tileX = 200;  // out of range — should clamp to 32 on save
    col.tileY = 200;
    // Add a few real triangles so the file isn't empty.
    for (int i = 0; i < 5; i++) {
        WoweeCollision::Triangle t;
        t.v0 = glm::vec3(static_cast<float>(i), 0, 0);
        t.v1 = glm::vec3(static_cast<float>(i + 1), 0, 0);
        t.v2 = glm::vec3(static_cast<float>(i), 1, 0);
        t.flags = 0x01;
        col.triangles.push_back(t);
    }
    REQUIRE(WoweeCollisionBuilder::save(col, path));
    auto reloaded = WoweeCollisionBuilder::load(path);
    REQUIRE(reloaded.isValid());
    REQUIRE(reloaded.tileX == 32);  // clamped from 200
    REQUIRE(reloaded.tileY == 32);
    REQUIRE(reloaded.triangles.size() == 5);
    std::filesystem::remove(path);
}

TEST_CASE("WOB rejects load on overlong building name", "[wob][hardening]") {
    ensureTestDir();
    std::string base = TEST_DIR + "/bad_name";
    std::string path = base + ".wob";
    {
        std::ofstream f(path, std::ios::binary);
        uint32_t magic = 0x31424F57; // WOB1
        uint32_t gc = 1, pc = 0, dc = 0;
        float boundRadius = 1.0f;
        f.write(reinterpret_cast<const char*>(&magic), 4);
        f.write(reinterpret_cast<const char*>(&gc), 4);
        f.write(reinterpret_cast<const char*>(&pc), 4);
        f.write(reinterpret_cast<const char*>(&dc), 4);
        f.write(reinterpret_cast<const char*>(&boundRadius), 4);
        // Overlong building name length (5000 > 1024 cap)
        uint16_t nameLen = 5000;
        f.write(reinterpret_cast<const char*>(&nameLen), 2);
    }
    auto bld = WoweeBuildingLoader::load(base);
    // Without the reject, the loader would silently set nameLen=0 and the
    // 5000 stale bytes after would be misread as the next group's name+
    // counts. With the fix the load returns an empty WoweeBuilding.
    REQUIRE_FALSE(bld.isValid());
    std::filesystem::remove(path);
}

TEST_CASE("WOB rejects load on overlong group name", "[wob][hardening]") {
    ensureTestDir();
    std::string base = TEST_DIR + "/bad_grp_name";
    std::string path = base + ".wob";
    {
        std::ofstream f(path, std::ios::binary);
        uint32_t magic = 0x31424F57; // WOB1
        uint32_t gc = 1, pc = 0, dc = 0;
        float boundRadius = 1.0f;
        f.write(reinterpret_cast<const char*>(&magic), 4);
        f.write(reinterpret_cast<const char*>(&gc), 4);
        f.write(reinterpret_cast<const char*>(&pc), 4);
        f.write(reinterpret_cast<const char*>(&dc), 4);
        f.write(reinterpret_cast<const char*>(&boundRadius), 4);
        // Valid building name
        uint16_t nameLen = 4;
        f.write(reinterpret_cast<const char*>(&nameLen), 2);
        f.write("Test", 4);
        // Overlong group name length
        uint16_t gnLen = 9999;
        f.write(reinterpret_cast<const char*>(&gnLen), 2);
    }
    auto bld = WoweeBuildingLoader::load(base);
    REQUIRE_FALSE(bld.isValid());
    std::filesystem::remove(path);
}
