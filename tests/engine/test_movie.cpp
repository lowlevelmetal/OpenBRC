#include "TestData.h"

#include "audio/Mixer.h"
#include "engine/Engine.h"
#include "engine/Movie.h"

#include <gtest/gtest.h>

namespace brc {
namespace {

// A host with a manual clock and no screen.
class FakeHost final : public engine::Host {
public:
    std::uint32_t ticks() override { return now; }
    void present(const engine::Image8&, const std::array<std::uint32_t, 256>&) override { ++presents; }
    std::pair<int, int> mousePosition() override { return {0, 0}; }
    bool leftButtonDown() override { return false; }
    void warpMouse(int, int) override {}
    void keepAlive() override { ++now; }

    std::uint32_t now = 1000;
    int presents = 0;
};

class TestNode final : public engine::Node {
public:
    using engine::Node::Node;
    void movieDone(const std::string& movie) override {
        doneName = movie;
        engine::Node::movieDone(movie);
    }
    std::string doneName;
};

class TestEngine final : public engine::Engine {
public:
    using engine::Engine::Engine;
    std::unique_ptr<engine::Node> createNode(const std::string& name) override {
        return std::make_unique<TestNode>(*this, name);
    }
};

TEST(Movie, IntroPlaysToTheEndAndReportsIt) {
    BRC_REQUIRE_GAME_DATA();
    FakeHost host;
    audio::Mixer mixer;
    TestEngine engine(*test::gameData(), host, &mixer);
    ASSERT_TRUE(engine.resources().open("intromovie.omt"));
    TestNode node(engine, "intromovie");
    engine::HcMovie movie(engine, node);
    ASSERT_TRUE(movie.open("smk\\Intro.smk", false, false, false, true, 0, true));
    EXPECT_EQ(movie.width(), 512);
    EXPECT_EQ(movie.height(), 384);
    EXPECT_EQ(movie.frame(), 1u); // the first frame was shown at once
    movie.setPaused(false);
    const std::uint32_t frames = movie.frameCount();
    for (int pass = 0; pass < 200000 && !movie.done(); ++pass) {
        host.now += 5;
        movie.spendTime();
    }
    EXPECT_TRUE(movie.done());
    EXPECT_EQ(node.doneName, "Intro.smk");
    // 10 fps: the whole movie takes about frames * 100 ms of game time.
    EXPECT_NEAR(static_cast<double>(host.now - 1000), frames * 100.0, frames * 10.0);
    // Its audio went to a mixer stream.
    EXPECT_GE(mixer.voiceCount(), 1u);
}

TEST(Movie, MissingFileFailsCleanly) {
    BRC_REQUIRE_GAME_DATA();
    FakeHost host;
    TestEngine engine(*test::gameData(), host, nullptr);
    ASSERT_TRUE(engine.resources().open("intromovie.omt"));
    TestNode node(engine, "intromovie");
    engine::HcMovie movie(engine, node);
    EXPECT_FALSE(movie.open("smk\\NoSuchMovie.smk"));
    EXPECT_FALSE(movie.opened());
}

} // namespace
} // namespace brc
