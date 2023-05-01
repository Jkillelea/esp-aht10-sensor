#pragma once

#include <cstddef>
#include <vector>
#include <memory>
#include <Print.h>

// DebugStream: holds pointers to various output streams (serial, display, mqtt, etc)
// for debugging use. Allows user to select a current stream or seamlessly broadcast
// messages on all interfaces.
class DebugStream : public Print {
public:
    DebugStream() {
        // nothing
    }

    DebugStream(Print *stream) {
        this->add_stream(stream);
    }

    // Add a stream to the list of streams
    void add_stream(Print *stream, bool new_default = true) {
        this->output_streams.push_back(stream);

        if (new_default) {
            this->set_stream(this->output_streams[this->output_streams.size() - 1]);
        }
    }

    // Select a stream to use as output, such as select_stream(Serial)
    void set_stream(Print *stream) {
        for (auto s : this->output_streams) {
            if (s == stream) {
                this->current_stream = s;
            }
        }
    }

    // Broadcast writes on all interfaces
    void set_broadcast(bool enabled = true) {
        this->broadcast = enabled;
    }
    
    // Overloaded methods
    size_t write(uint8_t data) {
        size_t rv = -1;

        if (!this->broadcast) {
            if (this->current_stream != nullptr) {
                rv = this->current_stream->write(data);
            }
        } else {
            for (auto stream : this->output_streams) {
                if (stream != nullptr) {
                    rv = stream->write(data);
                    if (rv != 1)
                        break;
                }
            }
        }

        return rv;
    }

    // Overloaded methods
    size_t write(const uint8_t *buffer, size_t size) {
        size_t rv = -1;

        if (!this->broadcast) {
            if (this->current_stream != nullptr) {
                rv = this->current_stream->write(buffer, size);
            }
        } else {
            for (auto stream : this->output_streams) {
                rv = stream->write(buffer, size);
                if (rv != size)
                    break;
            }

        }

        return rv;
    }

private:
    bool broadcast = false;
    Print *current_stream = nullptr;
    std::vector<Print *> output_streams;
};

