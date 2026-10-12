#pragma once
#include <QByteArray>
#include <algorithm>
#include <stdexcept>

// Decode incrementally: a large HTTP chunk must contribute to throughput as
// it arrives, rather than waiting for its complete body before counting it.
class SpeedBody {
public:
    SpeedBody(bool chunked, qint64 length, qint64 limit)
        : chunked_(chunked), length_(length), limit_(limit) {
        if (limit < 0 || length < -1 || length > limit) fail();
    }
    QByteArray feed(QByteArray bytes) {
        if (complete_) { if (!bytes.isEmpty()) fail(); return {}; }
        buffer_ += bytes;
        QByteArray payload;
        if (!chunked_) {
            payload = std::move(buffer_); buffer_.clear();
            count(payload.size());
            complete_ = length_ >= 0 && received_ == length_;
            return payload;
        }
        while (!complete_) {
            if (trailers_) {
                const auto end = buffer_.indexOf("\r\n");
                if (end < 0) { if (buffer_.size() > 16384) fail(); break; }
                trailerBytes_ += end + 2;
                if (trailerBytes_ > 16384) fail();
                buffer_.remove(0, end + 2);
                if (!end) { complete_ = true; if (!buffer_.isEmpty()) fail(); }
                continue;
            }
            if (needCrlf_) {
                if (buffer_.size() < 2) break;
                if (!buffer_.startsWith("\r\n")) fail();
                buffer_.remove(0, 2); needCrlf_ = false; remaining_ = -1;
            }
            if (remaining_ < 0) {
                const auto end = buffer_.indexOf("\r\n");
                if (end < 0) { if (buffer_.size() > 128) fail(); break; }
                const auto token = buffer_.left(end).split(';').front();
                if (token.isEmpty() || token.size() > 16) fail();
                for (const char c : token) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) fail();
                bool ok = false; remaining_ = token.toLongLong(&ok, 16);
                if (!ok || remaining_ < 0 || remaining_ > limit_ - received_) fail();
                buffer_.remove(0, end + 2);
                if (!remaining_) { trailers_ = true; continue; }
            }
            const auto n = std::min<qint64>(remaining_, buffer_.size());
            payload += buffer_.left(n); buffer_.remove(0, n);
            count(n); remaining_ -= n;
            if (!remaining_) needCrlf_ = true;
            if (buffer_.isEmpty()) break;
        }
        return payload;
    }
    void finishEof() {
        if (chunked_ ? !complete_ : length_ >= 0 && received_ != length_) fail();
        complete_ = true;
    }
    bool complete() const { return complete_; }
    qint64 received() const { return received_; }
private:
    [[noreturn]] static void fail() { throw std::runtime_error("测速响应数据无效或被截断"); }
    void count(qint64 n) {
        if (n > limit_ - received_ || (!chunked_ && length_ >= 0 && n > length_ - received_)) fail();
        received_ += n;
    }
    bool chunked_, complete_ = false, needCrlf_ = false, trailers_ = false;
    qint64 length_, limit_, received_ = 0, remaining_ = -1, trailerBytes_ = 0;
    QByteArray buffer_;
};
