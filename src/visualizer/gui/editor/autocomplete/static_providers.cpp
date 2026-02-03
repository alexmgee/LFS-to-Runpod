/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "static_providers.hpp"
#include <algorithm>
#include <optional>

namespace lfs::vis::editor {

    namespace {

        bool isWordBoundary(const std::string& str, size_t pos) {
            if (pos == 0)
                return true;
            const char prev = str[pos - 1];
            const char curr = str[pos];
            if (prev == '_')
                return true;
            if (std::islower(prev) && std::isupper(curr))
                return true;
            return false;
        }

        std::optional<int> fuzzyScore(const std::string& str, const std::string& pattern) {
            if (pattern.empty())
                return 1000;
            if (pattern.size() > str.size())
                return std::nullopt;

            int score = 0;
            size_t patternIdx = 0;
            bool isPrefix = true;
            int boundaryMatches = 0;

            for (size_t i = 0; i < str.size() && patternIdx < pattern.size(); ++i) {
                const char s = std::tolower(static_cast<unsigned char>(str[i]));
                const char p = std::tolower(static_cast<unsigned char>(pattern[patternIdx]));

                if (s == p) {
                    if (i == patternIdx) {
                        score += 10;
                    }

                    if (isWordBoundary(str, i)) {
                        ++boundaryMatches;
                        score += 5;
                    }

                    ++patternIdx;
                } else {
                    isPrefix = false;
                }
            }

            if (patternIdx < pattern.size()) {
                return std::nullopt;
            }

            if (isPrefix) {
                score += 100;
            }

            score += boundaryMatches * 10;

            return score;
        }

    } // namespace

    std::vector<CompletionItem> PythonKeywordsProvider::getCompletions(
        const std::string& prefix, const std::string& /*context*/) {
        static const std::vector<std::pair<const char*, const char*>> keywords = {
            {"False", "Boolean constant"},
            {"None", "Null constant"},
            {"True", "Boolean constant"},
            {"and", "Logical AND operator"},
            {"as", "Alias in import/with statements"},
            {"assert", "Debugging assertion"},
            {"async", "Async function/context manager"},
            {"await", "Await async result"},
            {"break", "Exit loop"},
            {"class", "Define a class"},
            {"continue", "Continue to next iteration"},
            {"def", "Define a function"},
            {"del", "Delete a reference"},
            {"elif", "Else if condition"},
            {"else", "Else clause"},
            {"except", "Handle exception"},
            {"finally", "Always execute block"},
            {"for", "For loop"},
            {"from", "Import from module"},
            {"global", "Global variable declaration"},
            {"if", "Conditional statement"},
            {"import", "Import module"},
            {"in", "Membership/iteration operator"},
            {"is", "Identity comparison"},
            {"lambda", "Anonymous function"},
            {"nonlocal", "Nonlocal variable declaration"},
            {"not", "Logical NOT operator"},
            {"or", "Logical OR operator"},
            {"pass", "No-op statement"},
            {"raise", "Raise an exception"},
            {"return", "Return from function"},
            {"try", "Try block for exceptions"},
            {"while", "While loop"},
            {"with", "Context manager statement"},
            {"yield", "Generator yield"},
        };

        std::vector<CompletionItem> results;
        for (const auto& [kw, desc] : keywords) {
            if (auto score = fuzzyScore(kw, prefix)) {
                results.push_back({kw, kw, desc, CompletionKind::Keyword, *score});
            }
        }
        return results;
    }

    std::vector<CompletionItem> PythonBuiltinsProvider::getCompletions(
        const std::string& prefix, const std::string& /*context*/) {
        static const std::vector<std::tuple<const char*, const char*, const char*>> builtins = {
            {"abs", "abs(x)", "Return absolute value"},
            {"all", "all(iterable)", "Return True if all elements are true"},
            {"any", "any(iterable)", "Return True if any element is true"},
            {"ascii", "ascii(obj)", "Return ASCII representation"},
            {"bin", "bin(x)", "Convert to binary string"},
            {"bool", "bool([x])", "Convert to boolean"},
            {"breakpoint", "breakpoint()", "Enter debugger"},
            {"bytearray", "bytearray([source])", "Mutable bytes sequence"},
            {"bytes", "bytes([source])", "Immutable bytes sequence"},
            {"callable", "callable(obj)", "Check if callable"},
            {"chr", "chr(i)", "Return character from code point"},
            {"classmethod", "@classmethod", "Class method decorator"},
            {"compile", "compile(source, filename, mode)", "Compile source to code"},
            {"complex", "complex([real, imag])", "Create complex number"},
            {"delattr", "delattr(obj, name)", "Delete attribute"},
            {"dict", "dict(**kwargs)", "Create dictionary"},
            {"dir", "dir([obj])", "List attributes"},
            {"divmod", "divmod(a, b)", "Return quotient and remainder"},
            {"enumerate", "enumerate(iterable, start=0)", "Return enumerate object"},
            {"eval", "eval(expression)", "Evaluate expression"},
            {"exec", "exec(code)", "Execute code"},
            {"filter", "filter(func, iterable)", "Filter elements"},
            {"float", "float([x])", "Convert to float"},
            {"format", "format(value, format_spec)", "Format value"},
            {"frozenset", "frozenset([iterable])", "Immutable set"},
            {"getattr", "getattr(obj, name[, default])", "Get attribute"},
            {"globals", "globals()", "Return global symbol table"},
            {"hasattr", "hasattr(obj, name)", "Check attribute exists"},
            {"hash", "hash(obj)", "Return hash value"},
            {"help", "help([obj])", "Interactive help"},
            {"hex", "hex(x)", "Convert to hex string"},
            {"id", "id(obj)", "Return object identity"},
            {"input", "input([prompt])", "Read line from input"},
            {"int", "int([x, base])", "Convert to integer"},
            {"isinstance", "isinstance(obj, classinfo)", "Check instance type"},
            {"issubclass", "issubclass(cls, classinfo)", "Check subclass"},
            {"iter", "iter(obj[, sentinel])", "Return iterator"},
            {"len", "len(s)", "Return length"},
            {"list", "list([iterable])", "Create list"},
            {"locals", "locals()", "Return local symbol table"},
            {"map", "map(func, *iterables)", "Apply function to elements"},
            {"max", "max(iterable, *[, key, default])", "Return maximum"},
            {"memoryview", "memoryview(obj)", "Create memory view"},
            {"min", "min(iterable, *[, key, default])", "Return minimum"},
            {"next", "next(iterator[, default])", "Get next item"},
            {"object", "object()", "Base class"},
            {"oct", "oct(x)", "Convert to octal string"},
            {"open", "open(file, mode='r')", "Open file"},
            {"ord", "ord(c)", "Return code point"},
            {"pow", "pow(base, exp[, mod])", "Return power"},
            {"print", "print(*objects, sep=' ', end='\\n')", "Print objects"},
            {"property", "@property", "Property decorator"},
            {"range", "range(stop) or range(start, stop[, step])", "Return range"},
            {"repr", "repr(obj)", "Return representation"},
            {"reversed", "reversed(seq)", "Return reversed iterator"},
            {"round", "round(number[, ndigits])", "Round number"},
            {"set", "set([iterable])", "Create set"},
            {"setattr", "setattr(obj, name, value)", "Set attribute"},
            {"slice", "slice(stop) or slice(start, stop[, step])", "Create slice"},
            {"sorted", "sorted(iterable, *, key=None, reverse=False)", "Return sorted list"},
            {"staticmethod", "@staticmethod", "Static method decorator"},
            {"str", "str([obj])", "Convert to string"},
            {"sum", "sum(iterable, /, start=0)", "Sum of items"},
            {"super", "super([type, obj])", "Return superclass"},
            {"tuple", "tuple([iterable])", "Create tuple"},
            {"type", "type(obj) or type(name, bases, dict)", "Return type"},
            {"vars", "vars([obj])", "Return __dict__"},
            {"zip", "zip(*iterables)", "Zip iterables together"},
            // Common exceptions
            {"Exception", "Exception(*args)", "Base exception class"},
            {"TypeError", "TypeError(*args)", "Type error"},
            {"ValueError", "ValueError(*args)", "Value error"},
            {"KeyError", "KeyError(*args)", "Key error"},
            {"IndexError", "IndexError(*args)", "Index error"},
            {"AttributeError", "AttributeError(*args)", "Attribute error"},
            {"RuntimeError", "RuntimeError(*args)", "Runtime error"},
            {"ImportError", "ImportError(*args)", "Import error"},
            {"FileNotFoundError", "FileNotFoundError(*args)", "File not found error"},
        };

        std::vector<CompletionItem> results;
        for (const auto& [name, sig, desc] : builtins) {
            if (auto score = fuzzyScore(name, prefix)) {
                results.push_back({name, sig, desc, CompletionKind::Builtin, *score});
            }
        }
        return results;
    }

    std::vector<CompletionItem> LichtfeldApiProvider::getCompletions(
        const std::string& prefix, const std::string& context) {
        // Top-level lichtfeld API
        static const std::vector<std::tuple<const char*, const char*, const char*, CompletionKind>> top_level = {
            {"import lichtfeld as lf", "import lichtfeld as lf", "Import lichtfeld module", CompletionKind::Module},
            {"lichtfeld", "lichtfeld", "LichtFeld Python module", CompletionKind::Module},
            {"lf", "lf", "LichtFeld module alias", CompletionKind::Module},
        };

        // lf.* functions
        static const std::vector<std::tuple<const char*, const char*, const char*, CompletionKind>> lf_api = {
            {"get_scene", "lf.get_scene()", "Get current scene (None if unavailable)", CompletionKind::Function},
            {"context", "lf.context()", "Get training context view", CompletionKind::Function},
            {"gaussians", "lf.gaussians()", "Get Gaussians info view", CompletionKind::Function},
            {"session", "lf.session()", "Get training session", CompletionKind::Function},
            {"train_cameras", "lf.train_cameras()", "Get training cameras", CompletionKind::Function},
            {"val_cameras", "lf.val_cameras()", "Get validation cameras", CompletionKind::Function},
            // Decorators
            {"on_training_start", "@lf.on_training_start", "Training start callback decorator", CompletionKind::Decorator},
            {"on_iteration_start", "@lf.on_iteration_start", "Iteration start callback decorator", CompletionKind::Decorator},
            {"on_post_step", "@lf.on_post_step", "Post-step callback decorator", CompletionKind::Decorator},
            {"on_pre_optimizer_step", "@lf.on_pre_optimizer_step", "Pre-optimizer callback decorator", CompletionKind::Decorator},
            {"on_training_end", "@lf.on_training_end", "Training end callback decorator", CompletionKind::Decorator},
            // Submodules
            {"scene", "lf.scene", "Scene graph submodule", CompletionKind::Module},
            {"handlers", "lf.handlers", "Event handlers submodule", CompletionKind::Module},
        };

        // Context properties
        static const std::vector<std::tuple<const char*, const char*, const char*, CompletionKind>> context_api = {
            {"iteration", "ctx.iteration", "Current iteration number", CompletionKind::Property},
            {"max_iterations", "ctx.max_iterations", "Maximum iterations", CompletionKind::Property},
            {"loss", "ctx.loss", "Current loss value", CompletionKind::Property},
            {"num_gaussians", "ctx.num_gaussians", "Number of Gaussians", CompletionKind::Property},
            {"is_refining", "ctx.is_refining", "Whether in refining phase", CompletionKind::Property},
            {"is_training", "ctx.is_training", "Whether training is active", CompletionKind::Property},
            {"is_paused", "ctx.is_paused", "Whether training is paused", CompletionKind::Property},
            {"phase", "ctx.phase", "Current training phase", CompletionKind::Property},
            {"strategy", "ctx.strategy", "Training strategy name", CompletionKind::Property},
        };

        // Session API
        static const std::vector<std::tuple<const char*, const char*, const char*, CompletionKind>> session_api = {
            {"optimizer", "session.optimizer()", "Get optimizer view", CompletionKind::Function},
            {"model", "session.model()", "Get model view", CompletionKind::Function},
            {"pause", "session.pause()", "Pause training", CompletionKind::Function},
            {"resume", "session.resume()", "Resume training", CompletionKind::Function},
            {"request_stop", "session.request_stop()", "Request training stop", CompletionKind::Function},
        };

        // Optimizer API
        static const std::vector<std::tuple<const char*, const char*, const char*, CompletionKind>> optimizer_api = {
            {"scale_lr", "optimizer.scale_lr(factor)", "Scale learning rate by factor", CompletionKind::Function},
            {"set_lr", "optimizer.set_lr(value)", "Set learning rate", CompletionKind::Function},
            {"get_lr", "optimizer.get_lr()", "Get current learning rate", CompletionKind::Function},
        };

        // Model API
        static const std::vector<std::tuple<const char*, const char*, const char*, CompletionKind>> model_api = {
            {"clamp", "model.clamp(attr, min=None, max=None)", "Clamp attribute values", CompletionKind::Function},
            {"scale", "model.scale(attr, factor)", "Scale attribute by factor", CompletionKind::Function},
            {"set", "model.set(attr, value)", "Set attribute value", CompletionKind::Function},
        };

        // Scene API
        static const std::vector<std::tuple<const char*, const char*, const char*, CompletionKind>> scene_api = {
            {"root", "scene.root", "Scene root node", CompletionKind::Property},
            {"find_node", "scene.find_node(name)", "Find node by name", CompletionKind::Function},
            {"get_training_model", "scene.get_training_model()", "Get training SplatData", CompletionKind::Function},
        };

        // SceneNode API
        static const std::vector<std::tuple<const char*, const char*, const char*, CompletionKind>> node_api = {
            {"name", "node.name", "Node name", CompletionKind::Property},
            {"parent", "node.parent", "Parent node", CompletionKind::Property},
            {"children", "node.children", "Child nodes list", CompletionKind::Property},
            {"splat_data", "node.splat_data", "Node's SplatData (if any)", CompletionKind::Property},
            {"is_training_model", "node.is_training_model", "Whether this is training model", CompletionKind::Property},
        };

        // SplatData API
        static const std::vector<std::tuple<const char*, const char*, const char*, CompletionKind>> splat_data_api = {
            {"size", "splat_data.size", "Number of Gaussians", CompletionKind::Property},
            {"xyz", "splat_data.xyz", "Positions tensor [N, 3]", CompletionKind::Property},
            {"rgb", "splat_data.rgb", "Colors tensor [N, 3]", CompletionKind::Property},
            {"opacity", "splat_data.opacity", "Opacities tensor [N, 1]", CompletionKind::Property},
            {"scales", "splat_data.scales", "Scales tensor [N, 3]", CompletionKind::Property},
            {"rotations", "splat_data.rotations", "Rotations tensor [N, 4]", CompletionKind::Property},
            {"sh_degree", "splat_data.sh_degree", "Active SH degree", CompletionKind::Property},
            {"features_dc", "splat_data.features_dc", "DC SH features [N, 1, 3]", CompletionKind::Property},
            {"features_rest", "splat_data.features_rest", "Rest SH features", CompletionKind::Property},
        };

        // Tensor API
        static const std::vector<std::tuple<const char*, const char*, const char*, CompletionKind>> tensor_api = {
            {"shape", "tensor.shape", "Tensor shape tuple", CompletionKind::Property},
            {"dtype", "tensor.dtype", "Data type string", CompletionKind::Property},
            {"device", "tensor.device", "Device string (cuda/cpu)", CompletionKind::Property},
            {"numel", "tensor.numel()", "Number of elements", CompletionKind::Function},
            {"to_numpy", "tensor.to_numpy()", "Convert to numpy array", CompletionKind::Function},
        };

        // Camera API
        static const std::vector<std::tuple<const char*, const char*, const char*, CompletionKind>> camera_api = {
            {"name", "camera.name", "Camera name", CompletionKind::Property},
            {"width", "camera.width", "Image width", CompletionKind::Property},
            {"height", "camera.height", "Image height", CompletionKind::Property},
            {"fx", "camera.fx", "Focal length X", CompletionKind::Property},
            {"fy", "camera.fy", "Focal length Y", CompletionKind::Property},
        };

        std::vector<CompletionItem> results;

        auto addMatching = [&](const auto& items, int basePriority) {
            for (const auto& [name, sig, desc, kind] : items) {
                if (auto score = fuzzyScore(name, prefix)) {
                    results.push_back({name, sig, desc, kind, basePriority + *score});
                }
            }
        };

        // Determine context for smarter completions
        bool isLfContext = context.find("lf.") != std::string::npos ||
                           context.find("lichtfeld.") != std::string::npos;
        bool isContextCtx = context.find("context") != std::string::npos ||
                            context.find("ctx") != std::string::npos;
        bool isSessionCtx = context.find("session") != std::string::npos;
        bool isOptimizerCtx = context.find("optimizer") != std::string::npos;
        bool isModelCtx = context.find("model") != std::string::npos;
        bool isSceneCtx = context.find("scene") != std::string::npos;
        bool isNodeCtx = context.find("node") != std::string::npos;
        bool isSplatCtx = context.find("splat") != std::string::npos;
        bool isTensorCtx = context.find("tensor") != std::string::npos ||
                           context.find(".xyz") != std::string::npos ||
                           context.find(".rgb") != std::string::npos;
        bool isCameraCtx = context.find("camera") != std::string::npos;

        // Add contextual completions
        if (isOptimizerCtx) {
            addMatching(optimizer_api, 95);
        } else if (isModelCtx) {
            addMatching(model_api, 95);
        } else if (isSessionCtx) {
            addMatching(session_api, 95);
        } else if (isContextCtx) {
            addMatching(context_api, 95);
        } else if (isNodeCtx) {
            addMatching(node_api, 95);
        } else if (isSplatCtx) {
            addMatching(splat_data_api, 95);
        } else if (isTensorCtx) {
            addMatching(tensor_api, 95);
        } else if (isCameraCtx) {
            addMatching(camera_api, 95);
        } else if (isSceneCtx) {
            addMatching(scene_api, 95);
        } else if (isLfContext) {
            addMatching(lf_api, 95);
        } else {
            // Top-level context - show imports and common patterns
            addMatching(top_level, 85);
            addMatching(lf_api, 80);
        }

        return results;
    }

    std::vector<std::unique_ptr<ISymbolProvider>> createStaticProviders() {
        std::vector<std::unique_ptr<ISymbolProvider>> providers;
        providers.push_back(std::make_unique<PythonKeywordsProvider>());
        providers.push_back(std::make_unique<PythonBuiltinsProvider>());
        providers.push_back(std::make_unique<LichtfeldApiProvider>());
        return providers;
    }

} // namespace lfs::vis::editor
