#pragma once

// Cola bloqueante y thread-safe.
//
// En el diseño original ("Shm_Queue") esto representa una cola de comandos /
// mensajes compartida entre los distintos hilos del Control (Connection Point,
// Controller, Hardware Handlers y Msg Handler). Como el Control corre como un
// único proceso multihilo, la "memoria compartida" se implementa con una cola
// protegida por mutex + condition_variable en lugar de shm real.

#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>

template <class T>
class ShmQueue {
public:
    void push(T value) {
        {
            std::lock_guard<std::mutex> lk(m_);
            q_.push(std::move(value));
        }
        cv_.notify_one();
    }

    // Bloquea hasta que haya un elemento disponible.
    T pop() {
        std::unique_lock<std::mutex> lk(m_);
        cv_.wait(lk, [this] { return !q_.empty(); });
        T value = std::move(q_.front());
        q_.pop();
        return value;
    }

    // No bloqueante: devuelve nullopt si la cola está vacía.
    std::optional<T> try_pop() {
        std::lock_guard<std::mutex> lk(m_);
        if (q_.empty()) return std::nullopt;
        T value = std::move(q_.front());
        q_.pop();
        return value;
    }

    bool empty() {
        std::lock_guard<std::mutex> lk(m_);
        return q_.empty();
    }

private:
    std::queue<T> q_;
    std::mutex m_;
    std::condition_variable cv_;
};
