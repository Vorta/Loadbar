#include "model/placement.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>

namespace {
loadbar::Settings graphics_only() {
    return {.always_show_readout = false};
}
void require(bool value, const char *message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}
class AdjustedShell final : public loadbar::ShellPort {
  public:
    int adds{}, removes{}, queries{}, sets{};
    int query_length{}, set_length{};
    bool accepted{true}, corrupt{}, shorten_each_set{}, thin_result{};
    std::function<void()> callback;
    bool add() override {
        ++adds;
        return accepted;
    }
    void remove() noexcept override {
        ++removes;
    }
    static loadbar::Rect shorten(loadbar::Rect rectangle, loadbar::Edge edge, int length) {
        if (length > 0) {
            if (loadbar::horizontal(edge)) {
                rectangle.right = rectangle.left + length;
            } else {
                rectangle.bottom = rectangle.top + length;
            }
        }
        return rectangle;
    }
    loadbar::Rect query(loadbar::Rect rectangle, loadbar::Edge edge) override {
        ++queries;
        if (callback) {
            callback();
        }
        return shorten(rectangle, edge, query_length);
    }
    loadbar::Rect set(loadbar::Rect rectangle, loadbar::Edge edge) override {
        ++sets;
        if (corrupt) {
            return {};
        }
        if (thin_result) {
            rectangle.bottom = rectangle.top + 1;
        }
        const auto result = shorten(rectangle, edge, set_length);
        if (shorten_each_set) {
            set_length -= 40;
        }
        return result;
    }
};
std::vector<loadbar::Processor> hybrid_processors() {
    std::vector<loadbar::Processor> result;
    result.reserve(24);
    for (unsigned i = 0; i < 24; ++i) {
        result.push_back({{0, i}, i, true, {}, i < 8 ? 1U : 0U});
    }
    return result;
}
} // namespace
void run_placement_tests() {
    using namespace loadbar;
    const auto processors = hybrid_processors();
    const auto full_minimum =
        minimum_thickness(640, 300, loadbar::Edge::top, processors, 1, graphics_only(), 2);
    const auto short_minimum =
        minimum_thickness(560, 300, loadbar::Edge::top, processors, 1, graphics_only(), 2);
    require(short_minimum > full_minimum,
            "Fixture exercises a shortened edge needing extra thickness");
    for (const unsigned dpi : {96U, 120U, 144U, 192U}) {
        const float scale = static_cast<float>(dpi) / 96;
        const auto px = [scale](int dips) {
            return static_cast<int>(std::round(static_cast<float>(dips) * scale));
        };
        for (const auto edge : {Edge::top, Edge::bottom, Edge::left, Edge::right}) {
            const bool along_x = horizontal(edge);
            Monitor monitor{L"monitor",
                            L"Test monitor",
                            {-px(640), -px(1200), along_x ? 0 : px(560), along_x ? 0 : -px(560)},
                            dpi,
                            true};
            // Horizontal monitor is 640x1200 DIPs; vertical is 1200x640.
            Settings settings{.always_show_readout = false};
            settings.edge = edge;
            const auto saved = settings;
            for (const bool adjust_at_set : {false, true}) {
                AdjustedShell shell;
                shell.query_length = adjust_at_set ? 0 : px(560);
                shell.set_length = adjust_at_set ? px(560) : 0;
                {
                    AppBar bar(shell);
                    shell.callback = [&] { (void)bar.position(monitor.bounds, edge, px(40)); };
                    const auto placed =
                        place_readable_bar(bar, monitor, settings, processors, 1, 2);
                    require(placed && make_layout(
                                          static_cast<float>(placed->right - placed->left) / scale,
                                          static_cast<float>(placed->bottom - placed->top) / scale,
                                          edge, processors, 1, settings, 2)
                                          .fits,
                            "Every edge and DPI uses a readable negotiated region");
                    require(settings == saved && shell.adds == 1 && shell.queries <= 4,
                            "Retries preserve user choice and a single registration, without "
                            "recursion");
                    const int sets = shell.sets;
                    // Deliver notifications after the call, not only recursively within QUERY.
                    for (int notification = 0; notification < 5; ++notification) {
                        require(place_readable_bar(bar, monitor, settings, processors, 1, 2) ==
                                        placed &&
                                    shell.sets == sets,
                                "Deferred unchanged notifications cannot churn QUERY- or "
                                "SET-adjusted reservations");
                    }
                    // An observed QUERY span change drops the former SET-only short-edge hint.
                    shell.query_length = px(580);
                    shell.set_length = 0;
                    const auto expanded =
                        place_readable_bar(bar, monitor, settings, processors, 1, 2);
                    require(expanded &&
                                make_layout(
                                    static_cast<float>(expanded->right - expanded->left) / scale,
                                    static_cast<float>(expanded->bottom - expanded->top) / scale,
                                    edge, processors, 1, settings, 2)
                                    .fits,
                            "Edge growth reconsiders the settled constraint");
                    settings.thickness = 200;
                    const auto thick = place_readable_bar(bar, monitor, settings, processors, 1, 2);
                    settings.thickness = saved.thickness;
                    const auto thin = place_readable_bar(bar, monitor, settings, processors, 1, 2);
                    require(thick && thin &&
                                (along_x ? thin->bottom - thin->top < thick->bottom - thick->top
                                         : thin->right - thin->left < thick->right - thick->left),
                            "Settled geometry does not suppress explicit grow/shrink requests");
                    bar.invalidate_position();
                    require(
                        place_readable_bar(bar, monitor, settings, processors, 1, 2).has_value() &&
                            shell.adds == 1,
                        "Display invalidation renegotiates without duplicate registration");
                }
                require(shell.removes == 1, "Readable placement has balanced cleanup");
            }
        }
    }
    for (int failure = 0; failure < 4; ++failure) {
        AdjustedShell shell;
        shell.accepted = failure != 0;
        shell.corrupt = failure == 1;
        shell.query_length = failure == 2 ? 1 : 0;
        shell.thin_result = failure == 3;
        {
            AppBar bar(shell);
            Settings settings{.always_show_readout = false};
            settings.edge = Edge::top;
            const Monitor monitor{L"m", L"m", {-600, -1200, 0, 0}, 96, true};
            require(!place_readable_bar(bar, monitor, settings, processors, 1, 2),
                    "Rejected, invalid, impossible and nonprogressing Shell results fail");
            bar.remove();
            require(!bar.registered(), "Caller releases failed reservation promptly");
        }
        require(shell.queries <= 4 && shell.removes == (failure == 0 ? 0 : 1),
                "Failure retries are bounded and removal is idempotent");
    }
    {
        AdjustedShell shell;
        shell.query_length = 600;
        shell.set_length = 560;
        AppBar settled(shell);
        Settings settings{.always_show_readout = false};
        settings.edge = Edge::top;
        const Monitor monitor{L"m", L"m", {-640, -1200, 0, 0}, 96, true};
        const auto short_edge = place_readable_bar(settled, monitor, settings, processors, 1, 2);
        shell.query_length = 640;
        shell.set_length = 0;
        const auto full_edge = place_readable_bar(settled, monitor, settings, processors, 1, 2);
        require(short_edge && full_edge &&
                    full_edge->bottom - full_edge->top < short_edge->bottom - short_edge->top,
                "QUERY growth releases the old minimum and shrinks readable thickness");
        const int sets = shell.sets;
        require(place_readable_bar(settled, monitor, settings, processors, 1, 2) == full_edge &&
                    shell.sets == sets,
                "Recovered geometry also settles without SETPOS churn");
        const auto large_text = place_readable_bar(settled, monitor, settings, processors, 2, 2);
        require(large_text &&
                    large_text->bottom - large_text->top > full_edge->bottom - full_edge->top,
                "Changed text scale recomputes readability despite stable QUERY geometry");
        const auto fewer = std::vector<Processor>(processors.begin(), processors.begin() + 4);
        const auto smaller = place_readable_bar(settled, monitor, settings, fewer, 1, 1);
        require(smaller && smaller->bottom - smaller->top < large_text->bottom - large_text->top,
                "Changed topology/device count can lower the settled readable minimum");
    }
    AdjustedShell changing;
    changing.set_length = 560;
    changing.shorten_each_set = true;
    AppBar bar(changing);
    Settings settings{.always_show_readout = false};
    settings.edge = Edge::top;
    const auto result = place_readable_bar(bar, {L"m", L"m", {0, 0, 600, 1200}, 96, true}, settings,
                                           processors, 1, 2);
    require(changing.queries <= 4 && changing.sets <= 4, "Changing Shell replies cannot spin");
    if (result) {
        require(make_layout(static_cast<float>(result->right - result->left),
                            static_cast<float>(result->bottom - result->top), loadbar::Edge::top,
                            processors, 1, settings, 2)
                    .fits,
                "A bounded retry may only succeed with a complete readable layout");
    }
}
