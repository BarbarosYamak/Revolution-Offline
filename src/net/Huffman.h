#pragma once

#include "uo/types.h"

#include <vector>

namespace uo::net {

// Decompressor for the UO server->client Huffman stream.
//
// Mirrors client_2.0.7 Network_ProcessBuffer @ 0x42D8E0: walk a decode
// tree MSB-first; leaf value 256 is the flush marker, which discards the
// remaining bits in the current byte (byte-align to the next byte) and
// resets the walk to the root. Decoded bytes accumulate continuously —
// flush markers do NOT delimit output packets; framing is by length table.
//
// The decode tree is built from the same {bit_count, bit_value} encoding
// table the server compresses with (huffman.c), so the two always agree.
//
// Streaming: bit position persists across calls. A symbol that runs past
// the end of the buffered input is left undecoded and re-read once more
// bytes arrive; only whole consumed bytes are dropped.
class Huffman {
public:
    Huffman();

    // Append n compressed bytes and push every fully-decoded byte onto out.
    // Returns false only on a malformed code (a bit with no tree edge).
    bool Decompress(const u8* data, usize n, std::vector<u8>& out);

    void Reset();

private:
    struct Node {
        int child[2];  // index of bit-0 / bit-1 child, -1 = none
        int value;     // leaf value 0..256, or -1 for an internal node
    };

    int  NewNode();
    void Build();

    std::vector<Node> nodes_;  // nodes_[0] is the root
    std::vector<u8>   buf_;    // pending compressed bytes
    usize             bitPos_; // bit offset into buf_[0] (MSB-first), 0..7
};

}
