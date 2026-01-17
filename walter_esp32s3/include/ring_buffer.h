/**
 * @file ring_buffer.h
 * @brief Thread-safe ring buffer for ADC frame storage
 *
 * Lock-free single-producer single-consumer (SPSC) ring buffer optimized
 * for FreeRTOS. The ADC task is the sole producer, and consumers (telemetry,
 * webui) can read independently using cursors.
 */

#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <atomic>
#include "config.h"

// =============================================================================
// FRAME STRUCTURE
// =============================================================================

/**
 * @brief Single ADC sample frame
 */
struct AdcFrame {
    uint32_t    frameIndex;                     // Monotonic frame counter
    int64_t     startTimeUs;                    // UTC microseconds at frame start
    int64_t     endTimeUs;                      // UTC microseconds at frame end
    float       sampleRateHz;                   // Actual sample rate
    uint16_t    numSamples;                     // Number of samples in frame
    int32_t     samples[FRAME_SIZE];            // Raw 24-bit samples (sign-extended)
};

// =============================================================================
// RING BUFFER CLASS
// =============================================================================

/**
 * @brief Thread-safe ring buffer for ADC frames
 *
 * Design:
 * - Producer (ADC task) writes to head
 * - Multiple consumers can read using independent cursors
 * - Lock-free for producer, mutex protected for consumers
 * - Oldest frames overwritten when buffer full (drop-oldest policy)
 */
class AdcRingBuffer {
public:
    static constexpr size_t CAPACITY = RING_BUFFER_FRAMES;

    AdcRingBuffer() : _head(0), _frameCount(0), _droppedFrames(0), _mutex(nullptr), _newFrameSem(nullptr) {
        // Don't create FreeRTOS objects here - called before scheduler starts!
        memset(_frames, 0, sizeof(_frames));
    }

    ~AdcRingBuffer() {
        if (_mutex) vSemaphoreDelete(_mutex);
        if (_newFrameSem) vSemaphoreDelete(_newFrameSem);
    }

    /**
     * @brief Initialize FreeRTOS objects - call from setup() after scheduler starts
     */
    void begin() {
        if (!_mutex) {
            _mutex = xSemaphoreCreateMutex();
        }
        if (!_newFrameSem) {
            _newFrameSem = xSemaphoreCreateBinary();
        }
    }

    /**
     * @brief Push a new frame (producer only - ADC task)
     * @param frame Frame to push
     * @return true if successful
     *
     * This is called from the ADC ISR context or high-priority task.
     * Uses atomic operations to avoid blocking.
     */
    bool push(const AdcFrame& frame) {
        size_t idx = _head.load(std::memory_order_relaxed);

        // Copy frame to buffer
        memcpy(&_frames[idx], &frame, sizeof(AdcFrame));

        // Advance head (wraps around)
        _head.store((idx + 1) % CAPACITY, std::memory_order_release);

        // Track frame count
        uint32_t count = _frameCount.fetch_add(1, std::memory_order_relaxed);

        // Signal waiting consumers
        xSemaphoreGive(_newFrameSem);

        return true;
    }

    /**
     * @brief Get current head position (most recent frame)
     */
    size_t getHead() const {
        return _head.load(std::memory_order_acquire);
    }

    /**
     * @brief Get total frame count (monotonically increasing)
     */
    uint32_t getFrameCount() const {
        return _frameCount.load(std::memory_order_acquire);
    }

    /**
     * @brief Read frame at specific index
     * @param idx Buffer index (0 to CAPACITY-1)
     * @param frame Output frame
     * @return true if valid frame
     */
    bool readFrame(size_t idx, AdcFrame& frame) {
        if (idx >= CAPACITY) return false;

        if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            memcpy(&frame, &_frames[idx], sizeof(AdcFrame));
            xSemaphoreGive(_mutex);
            return frame.numSamples > 0;
        }
        return false;
    }

    /**
     * @brief Read most recent frame
     * @param frame Output frame
     * @return true if valid
     */
    bool readLatest(AdcFrame& frame) {
        size_t head = _head.load(std::memory_order_acquire);
        size_t idx = (head + CAPACITY - 1) % CAPACITY;  // Previous position is latest
        return readFrame(idx, frame);
    }

    /**
     * @brief Wait for new frame with timeout
     * @param timeoutMs Timeout in milliseconds
     * @return true if new frame available
     */
    bool waitForFrame(uint32_t timeoutMs) {
        return xSemaphoreTake(_newFrameSem, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
    }

    /**
     * @brief Get number of dropped frames
     */
    uint32_t getDroppedFrames() const {
        return _droppedFrames.load(std::memory_order_relaxed);
    }

    /**
     * @brief Increment dropped frame counter
     */
    void incrementDropped() {
        _droppedFrames.fetch_add(1, std::memory_order_relaxed);
    }

private:
    AdcFrame            _frames[CAPACITY];
    std::atomic<size_t> _head;
    std::atomic<uint32_t> _frameCount;
    std::atomic<uint32_t> _droppedFrames;
    SemaphoreHandle_t   _mutex;
    SemaphoreHandle_t   _newFrameSem;
};

// =============================================================================
// CONSUMER CURSOR
// =============================================================================

/**
 * @brief Consumer cursor for independent buffer reading
 *
 * Each consumer (telemetry, webui) maintains its own cursor to track
 * which frames it has processed, allowing independent read rates.
 */
class BufferCursor {
public:
    BufferCursor(AdcRingBuffer& buffer)
        : _buffer(buffer)
        , _lastFrameCount(0)
        , _lastReadIndex(0)
    {}

    /**
     * @brief Check if new frames are available
     */
    bool hasNewFrames() const {
        return _buffer.getFrameCount() > _lastFrameCount;
    }

    /**
     * @brief Get number of pending frames
     */
    uint32_t pendingFrames() const {
        uint32_t current = _buffer.getFrameCount();
        if (current > _lastFrameCount) {
            uint32_t pending = current - _lastFrameCount;
            // Cap at buffer capacity (older frames were overwritten)
            return (pending > AdcRingBuffer::CAPACITY) ? AdcRingBuffer::CAPACITY : pending;
        }
        return 0;
    }

    /**
     * @brief Read next frame for this consumer
     * @param frame Output frame
     * @return true if frame read successfully
     */
    bool readNext(AdcFrame& frame) {
        uint32_t currentCount = _buffer.getFrameCount();

        if (currentCount <= _lastFrameCount) {
            return false;  // No new frames
        }

        // Calculate buffer index for next frame
        uint32_t framesToSkip = 0;
        if (currentCount - _lastFrameCount > AdcRingBuffer::CAPACITY) {
            // We've fallen behind, skip to oldest available
            framesToSkip = (currentCount - _lastFrameCount) - AdcRingBuffer::CAPACITY;
            _lastFrameCount += framesToSkip;
        }

        size_t head = _buffer.getHead();
        size_t available = AdcRingBuffer::CAPACITY;
        size_t pendingCount = currentCount - _lastFrameCount;
        if (pendingCount > available) pendingCount = available;

        // Read oldest unread frame
        size_t idx = (head + AdcRingBuffer::CAPACITY - pendingCount) % AdcRingBuffer::CAPACITY;

        if (_buffer.readFrame(idx, frame)) {
            _lastFrameCount++;
            _lastReadIndex = idx;
            return true;
        }

        return false;
    }

    /**
     * @brief Wait for and read next frame
     * @param frame Output frame
     * @param timeoutMs Timeout in milliseconds
     * @return true if frame read
     */
    bool waitAndRead(AdcFrame& frame, uint32_t timeoutMs) {
        if (hasNewFrames()) {
            return readNext(frame);
        }

        if (_buffer.waitForFrame(timeoutMs)) {
            return readNext(frame);
        }

        return false;
    }

    /**
     * @brief Reset cursor to current position (skip all pending)
     */
    void reset() {
        _lastFrameCount = _buffer.getFrameCount();
    }

private:
    AdcRingBuffer&  _buffer;
    uint32_t        _lastFrameCount;
    size_t          _lastReadIndex;
};

#endif // RING_BUFFER_H
