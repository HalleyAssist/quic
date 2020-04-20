#include "quic/node_quic_buffer-inl.h"
#include "node_bob-inl.h"
#include "util-inl.h"
#include "uv.h"

#include "gtest/gtest.h"
#include <memory>
#include <vector>

using node::quic::QuicBuffer;
using node::quic::QuicBufferChunk;
using node::bob::Status;
using node::bob::Options;
using node::bob::Done;

TEST(QuicBuffer, Simple) {
  char data[100];
  memset(&data, 0, node::arraysize(data));
  uv_buf_t buf = uv_buf_init(data, node::arraysize(data));

  bool done = false;

  QuicBuffer buffer;
  buffer.Push(&buf, 1, [&](int status) {
    EXPECT_EQ(0, status);
    done = true;
  });

  buffer.Consume(100);
  ASSERT_EQ(0u, buffer.length());

  // We have to move the read head forward in order to consume
  buffer.Seek(1);
  buffer.Consume(100);
  ASSERT_TRUE(done);
  ASSERT_EQ(0u, buffer.length());
}

TEST(QuicBuffer, ConsumeMore) {
  char data[100];
  memset(&data, 0, node::arraysize(data));
  uv_buf_t buf = uv_buf_init(data, node::arraysize(data));

  bool done = false;

  QuicBuffer buffer;
  buffer.Push(&buf, 1, [&](int status) {
    EXPECT_EQ(0u, status);
    done = true;
  });

  buffer.Seek(1);
  buffer.Consume(150);  // Consume more than what was buffered
  ASSERT_TRUE(done);
  ASSERT_EQ(0u, buffer.length());
}

TEST(QuicBuffer, Multiple) {
  uv_buf_t bufs[] {
    uv_buf_init(const_cast<char*>("abcdefghijklmnopqrstuvwxyz"), 26),
    uv_buf_init(const_cast<char*>("zyxwvutsrqponmlkjihgfedcba"), 26)
  };

  QuicBuffer buf;
  bool done = false;
  buf.Push(bufs, 2, [&](int status) { done = true; });

  buf.Seek(2);
  ASSERT_EQ(buf.remaining(), 50);
  ASSERT_EQ(buf.length(), 52);

  buf.Consume(25);
  ASSERT_EQ(buf.length(), 27);

  buf.Consume(25);
  ASSERT_EQ(buf.length(), 2);

  buf.Consume(2);
  ASSERT_EQ(0u, buf.length());
}

TEST(QuicBuffer, Multiple2) {
  char* ptr = new char[100];
  memset(ptr, 0, 50);
  memset(ptr + 50, 1, 50);

  uv_buf_t bufs[] = {
    uv_buf_init(ptr, 50),
    uv_buf_init(ptr + 50, 50)
  };

  int count = 0;

  QuicBuffer buffer;
  buffer.Push(
      bufs, node::arraysize(bufs),
      [&](int status) {
    count++;
    ASSERT_EQ(0, status);
    delete[] ptr;
  });
  buffer.Seek(node::arraysize(bufs));

  buffer.Consume(25);
  ASSERT_EQ(75, buffer.length());
  buffer.Consume(25);
  ASSERT_EQ(50, buffer.length());
  buffer.Consume(25);
  ASSERT_EQ(25, buffer.length());
  buffer.Consume(25);
  ASSERT_EQ(0u, buffer.length());

  // The callback was only called once tho
  ASSERT_EQ(1, count);
}

TEST(QuicBuffer, Cancel) {
  char* ptr = new char[100];
  memset(ptr, 0, 50);
  memset(ptr + 50, 1, 50);

  uv_buf_t bufs[] = {
    uv_buf_init(ptr, 50),
    uv_buf_init(ptr + 50, 50)
  };

  int count = 0;

  QuicBuffer buffer;
  buffer.Push(
      bufs, node::arraysize(bufs),
      [&](int status) {
    count++;
    ASSERT_EQ(UV_ECANCELED, status);
    delete[] ptr;
  });

  buffer.Seek(1);
  buffer.Consume(25);
  ASSERT_EQ(75, buffer.length());
  buffer.Cancel();
  ASSERT_EQ(0, buffer.length());

  // The callback was only called once tho
  ASSERT_EQ(1, count);
}

TEST(QuicBuffer, Move) {
  QuicBuffer buffer1;
  QuicBuffer buffer2;

  char data[100];
  memset(&data, 0, node::arraysize(data));
  uv_buf_t buf = uv_buf_init(data, node::arraysize(data));

  buffer1.Push(&buf, 1);

  ASSERT_EQ(100, buffer1.length());

  buffer2 = std::move(buffer1);
  ASSERT_EQ(0u, buffer1.length());
  ASSERT_EQ(100, buffer2.length());
}

TEST(QuicBuffer, QuicBufferChunk) {
  std::unique_ptr<QuicBufferChunk> chunk =
      std::make_unique<QuicBufferChunk>(100);
  memset(chunk->out(), 1, 100);

  QuicBuffer buffer;
  buffer.Push(std::move(chunk));
  buffer.End();
  ASSERT_EQ(100, buffer.length());

  auto next = [&](
      int status,
      const ngtcp2_vec* data,
      size_t count,
      Done done) {
    ASSERT_EQ(status, Status::STATUS_END);
    ASSERT_EQ(count, 1);
    ASSERT_NE(data, nullptr);
    done(100);
  };

  ASSERT_EQ(buffer.remaining(), 100);

  ngtcp2_vec data[2];
  size_t len = sizeof(data) / sizeof(ngtcp2_vec);
  buffer.Pull(next, Options::OPTIONS_SYNC | Options::OPTIONS_END, data, len);

  ASSERT_EQ(buffer.remaining(), 0);

  buffer.Consume(50);
  ASSERT_EQ(50, buffer.length());

  buffer.Consume(50);
  ASSERT_EQ(0u, buffer.length());
}
