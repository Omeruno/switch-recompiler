// Included inside Scripts::Impl; each call derives the sender from this realm.
JSValue message_operation(const std::string &op, int argc, JSValueConst *args) {
    auto number = [&](int index) {
        uint32_t n = 0;
        if (index >= argc || JS_ToUint32(context, &n, args[index]) < 0)
            throw std::invalid_argument("Invalid message argument");
        return n;
    };
    auto port_array = [&](const std::vector<PortRef> &refs) {
        auto array = JS_NewArray(context);
        if (JS_IsException(array))
            return array;
        for (uint32_t i = 0; i < refs.size(); ++i) {
            auto pair = JS_NewArray(context);
            if (JS_IsException(pair)) {
                JS_FreeValue(context, array);
                return JS_EXCEPTION;
            }
            if (JS_SetPropertyUint32(context, pair, 0, JS_NewUint32(context, refs[i].id)) < 0 ||
                JS_SetPropertyUint32(context, pair, 1, JS_NewUint32(context, refs[i].epoch)) < 0) {
                JS_FreeValue(context, pair);
                JS_FreeValue(context, array);
                return JS_EXCEPTION;
            }
            if (JS_SetPropertyUint32(context, array, i, pair) < 0) {
                JS_FreeValue(context, array);
                return JS_EXCEPTION;
            }
        }
        return array;
    };
    if (op == "msgBuffer") {
        if (argc < 3)
            return JS_ThrowTypeError(context, "Missing clone buffer");
        size_t size = 0;
        auto *bytes = JS_GetArrayBuffer(context, &size, args[2]);
        if (!bytes && JS_HasException(context))
            return JS_EXCEPTION;
        if (size > MessageHub::packet_limit)
            return JS_ThrowRangeError(context, "Clone buffer quota");
        // Snapshot native bytes, without invoking a page's ArrayBuffer species constructor.
        return JS_NewArrayBufferCopy(context, bytes, size);
    }
    if (op == "msgInfo") {
        auto id = number(1);
        if (!id)
            id = message_window;
        auto *w = messages->window(id);
        auto result = JS_NewObject(context);
        JS_SetPropertyStr(context, result, "id", JS_NewUint32(context, id));
        JS_SetPropertyStr(context, result, "closed", JS_NewBool(context, !w || !w->alive));
        JS_SetPropertyStr(context, result, "parent",
                          JS_NewUint32(context, w && w->parent ? w->parent : id));
        JS_SetPropertyStr(context, result, "top", JS_NewUint32(context, messages->top(id)));
        JS_SetPropertyStr(context, result, "origin",
                          JS_NewString(context, w ? w->origin.c_str() : "null"));
        auto children = JS_NewArray(context);
        uint32_t index = 0;
        for (auto child : messages->children(id))
            JS_SetPropertyUint32(context, children, index++, JS_NewUint32(context, child));
        JS_SetPropertyStr(context, result, "children", children);
        return result;
    }
    if (op == "msgChannel") {
        auto refs = messages->channel(message_window);
        auto result = port_array(refs);
        if (JS_IsException(result))
            for (auto r : refs)
                messages->port_close(message_window, r);
        return result;
    }
    if (op == "msgStart" || op == "msgClose") {
        PortRef ref{number(1), number(2)};
        if (op == "msgStart")
            messages->port_start(message_window, ref);
        else
            messages->port_close(message_window, ref);
        return JS_UNDEFINED;
    }
    if (op == "msgPop") {
        MessagePacket packet;
        if (!messages->pop(message_window, packet))
            return JS_NULL;
        auto data =
            JS_ReadObject(context, packet.data.data(), packet.data.size(), JS_READ_OBJ_REFERENCE);
        if (JS_IsException(data)) {
            for (auto r : packet.ports)
                messages->port_close(message_window, r);
            return data;
        }
        auto result = JS_NewObject(context);
        if (JS_IsException(result)) {
            JS_FreeValue(context, data);
            for (auto r : packet.ports)
                messages->port_close(message_window, r);
            return result;
        }
        JS_SetPropertyStr(context, result, "graph", data);
        JS_SetPropertyStr(context, result, "source", JS_NewUint32(context, packet.source));
        JS_SetPropertyStr(context, result, "port", JS_NewUint32(context, packet.port));
        JS_SetPropertyStr(context, result, "epoch", JS_NewUint32(context, packet.port_epoch));
        JS_SetPropertyStr(context, result, "origin", JS_NewString(context, packet.origin.c_str()));
        JS_SetPropertyStr(context, result, "ports", port_array(packet.ports));
        if (JS_HasException(context)) {
            JS_FreeValue(context, result);
            for (auto r : packet.ports)
                messages->port_close(message_window, r);
            return JS_EXCEPTION;
        }
        return result;
    }
    if (op != "msgSend" && op != "msgPortSend" && op != "msgClone")
        return JS_ThrowTypeError(context, "Unknown message operation");
    if (argc < 6)
        return JS_ThrowTypeError(context, "Missing message argument");
    std::vector<PortRef> refs;
    std::vector<JSValue> buffers;
    auto release_buffers = [&]() {
        for (auto b : buffers)
            JS_FreeValue(context, b);
        buffers.clear();
    };
    try {
        auto length = [&](JSValueConst array) {
            auto value = JS_GetPropertyStr(context, array, "length");
            uint32_t n = 0;
            int ok = JS_ToUint32(context, &n, value);
            JS_FreeValue(context, value);
            if (ok < 0 || n > 64)
                throw std::length_error("Transfer quota");
            return n;
        };
        for (uint32_t i = 0, n = length(args[4]); i < n; ++i) {
            auto pair = JS_GetPropertyUint32(context, args[4], i);
            auto first = JS_GetPropertyUint32(context, pair, 0),
                 second = JS_GetPropertyUint32(context, pair, 1);
            PortRef r;
            int ok = JS_ToUint32(context, &r.id, first) | JS_ToUint32(context, &r.epoch, second);
            JS_FreeValue(context, first);
            JS_FreeValue(context, second);
            JS_FreeValue(context, pair);
            if (ok < 0)
                throw std::invalid_argument("Invalid port transfer");
            refs.push_back(r);
        }
        buffers.reserve(length(args[5]));
        for (uint32_t i = 0, n = length(args[5]); i < n; ++i) {
            auto buffer = JS_GetPropertyUint32(context, args[5], i);
            size_t bytes = 0;
            auto *data = JS_GetArrayBuffer(context, &bytes, buffer);
            if (!data && JS_HasException(context)) {
                JS_FreeValue(context, buffer);
                release_buffers();
                return JS_EXCEPTION;
            }
            if (game->owns_memory_buffer(buffer)) {
                JS_FreeValue(context, buffer);
                release_buffers();
                return JS_ThrowTypeError(context,
                                         "WebAssembly memory buffers cannot be transferred");
            }
            buffers.push_back(buffer);
        }
        size_t size = 0;
        auto *bytes = JS_WriteObject(context, &size, args[2], JS_WRITE_OBJ_REFERENCE);
        if (!bytes) {
            release_buffers();
            return JS_EXCEPTION;
        }
        if (size > MessageHub::packet_limit) {
            js_free(context, bytes);
            throw std::length_error("Message exceeds 256 KiB");
        }
        std::vector<uint8_t> wire;
        try {
            wire.assign(bytes, bytes + size);
        } catch (...) {
            js_free(context, bytes);
            throw;
        }
        js_free(context, bytes);
        JSValue result = JS_UNDEFINED;
        bool accepted = false;
        if (op == "msgClone") {
            result = JS_ReadObject(context, wire.data(), wire.size(), JS_READ_OBJ_REFERENCE);
            accepted = !JS_IsException(result);
            if (accepted) {
                auto packet = JS_NewObject(context);
                if (JS_IsException(packet)) {
                    JS_FreeValue(context, result);
                    release_buffers();
                    return packet;
                }
                JS_SetPropertyStr(context, packet, "graph", result);
                // Allocate JS references before committing native ownership.
                std::vector<PortRef> next;
                for (auto r : refs)
                    next.push_back({r.id, r.epoch + 1});
                auto js_ports = port_array(refs);
                for (uint32_t i = 0; i < next.size() && !JS_HasException(context); ++i) {
                    auto pair = JS_GetPropertyUint32(context, js_ports, i);
                    JS_SetPropertyUint32(context, pair, 1, JS_NewUint32(context, next[i].epoch));
                    JS_FreeValue(context, pair);
                }
                JS_SetPropertyStr(context, packet, "ports", js_ports);
                if (JS_HasException(context)) {
                    JS_FreeValue(context, packet);
                    release_buffers();
                    return JS_EXCEPTION;
                }
                try {
                    messages->clone_ports(message_window, refs);
                } catch (...) {
                    JS_FreeValue(context, packet);
                    throw;
                }
                result = packet;
            }
        } else if (op == "msgPortSend") {
            accepted =
                messages->post_port(message_window, {number(1), number(3)}, std::move(wire), refs);
            result = JS_NewBool(context, accepted);
        } else {
            auto requested = string(context, args[3], 2048);
            std::string target_origin;
            if (requested == "*")
                target_origin = "*";
            else if (requested == "/") {
                target_origin = messages->window(message_window)->origin;
                if (target_origin == "null")
                    target_origin = "/";
            } else {
                // Parse an actual HTTPS origin; paths do not participate in comparison.
                target_origin = origin(requested);
                if (requested.rfind("https://", 0) != 0 || target_origin.empty())
                    throw std::invalid_argument("Invalid targetOrigin");
            }
            auto target = number(1);
            accepted = messages->post_window(message_window, target ? target : message_window,
                                             std::move(target_origin), std::move(wire), refs);
            result = JS_NewBool(context, accepted);
        }
        if (accepted)
            for (auto b : buffers)
                JS_DetachArrayBuffer(context, b);
        release_buffers();
        return result;
    } catch (const std::length_error &e) {
        release_buffers();
        return JS_ThrowRangeError(context, "%s", e.what());
    } catch (...) {
        release_buffers();
        throw;
    }
}
