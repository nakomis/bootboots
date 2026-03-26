#pragma once

#include <Arduino.h>

/**
 * MotionDetector - Detects PIR motion sensor events via a direct GPIO pin
 *
 * Polls the PIR sensor on a configurable GPIO pin with:
 * - Edge-triggered detection (only triggers on LOW->HIGH transition)
 * - 200ms debounce to filter noise
 * - 30-second cooldown between activations
 */
class MotionDetector {
public:
    // Configuration constants
    static constexpr unsigned long DEBOUNCE_MS = 200;
    static constexpr unsigned long COOLDOWN_MS = 30000;  // 30 seconds

    /**
     * Constructor
     * @param pirPin GPIO pin number the PIR sensor is connected to (active HIGH)
     */
    explicit MotionDetector(int pirPin);

    /**
     * Update the motion detector state
     * Should be called frequently from the main loop
     */
    void update();

    /**
     * Check if motion was detected since last call
     * Returns true only once per motion event (edge-triggered)
     * @return true if new motion detected and cooldown has expired
     */
    bool wasMotionDetected();

    /**
     * Check if currently in cooldown period
     * @return true if cooldown is active
     */
    bool isInCooldown() const;

    /**
     * Get time remaining in cooldown (ms)
     * @return Milliseconds remaining, or 0 if not in cooldown
     */
    unsigned long getCooldownRemaining() const;

    /**
     * Reset the cooldown timer (for testing)
     */
    void resetCooldown();

    /**
     * Read the current raw PIR sensor state
     * @return true if PIR is active (HIGH)
     */
    bool readRawState() const;

private:
    int _pirPin;

    // State tracking
    bool _lastPinState;           // Previous pin state for edge detection
    bool _motionDetected;         // Flag set when motion detected
    unsigned long _lastDebounce;  // Time of last state change (debounce)
    unsigned long _cooldownStart; // Time cooldown started
    bool _inCooldown;             // Currently in cooldown period
};
