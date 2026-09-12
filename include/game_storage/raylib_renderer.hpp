#pragma once

#include "ui.hpp"
#include <raylib.h>

namespace game_storage::ui {

struct Theme {
    Color panel{23, 31, 44, 255}, header{32, 43, 59, 255};
    Color alternate{27, 36, 50, 255}, hover{40, 58, 75, 255};
    Color selected{37, 73, 84, 255}, text{230, 237, 244, 255};
    Color muted{151, 168, 188, 255}, border{54, 68, 86, 255};
    Color track{17, 24, 35, 255}, thumb{92, 118, 141, 255};
    Color accent{91, 215, 181, 255}, popup{35, 46, 63, 255};
    float font_size = 18, padding = 10, image_size = 28;
    Font font{}; // Borrowed; zero uses raylib's default font.
};

class RaylibRenderer {
public:
    // Optional resolver returns borrowed textures; the game owns their lifetime.
    using ImageResolver = std::function<Texture2D(const std::string&)>;
    // Called while clipped to each reserved region. nullopt denotes the shared footer.
    using CustomDrawer = std::function<void(std::optional<std::size_t> panel, Rect bounds)>;
    explicit RaylibRenderer(Theme theme = {}, ImageResolver resolver = {});
    ~RaylibRenderer(); // Destroy before CloseWindow().
    RaylibRenderer(const RaylibRenderer&) = delete;
    RaylibRenderer& operator=(const RaylibRenderer&) = delete;
    Theme& theme() noexcept { return theme_; }
    void set_custom_drawer(CustomDrawer drawer) { custom_drawer_ = std::move(drawer); }
    void clear_images(); // Clears owned PNG cache; also allows retrying failed paths.
    void draw(const Frame& frame, Point mouse);
    static Input poll_input();

private:
    Theme theme_;
    ImageResolver resolver_;
    CustomDrawer custom_drawer_;
    std::map<std::string, Texture2D> images_;
    Texture2D image(const std::string& key);
    void custom(std::optional<std::size_t> panel, Rect bounds);
    void cell(const Cell& content, Rect bounds, Rect clip, Color color);
    void text(const std::string& value, Rect bounds, Color color, float size);
};
} // namespace game_storage::ui
