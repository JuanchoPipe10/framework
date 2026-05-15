#!/usr/bin/env python3
"""
camera.py - Camera capture module for autonomous car
Captures frames from Delock 96403 camera (/dev/video0)
Ready to integrate with ML model
"""

import cv2
import time
import os
from datetime import datetime

# Configuration
CAMERA_INDEX    = 0           # /dev/video0
CAPTURE_WIDTH   = 640         # Reduced for better performance on ARM
CAPTURE_HEIGHT  = 480
CAPTURE_FPS     = 30
SAVE_DIR        = "/home/root/captures"
MAX_SAVED       = 10          # Keep only last N frames to save disk space


def init_camera():
    """Initialize camera and return capture object."""
    cap = cv2.VideoCapture(CAMERA_INDEX)

    if not cap.isOpened():
        print("[CAMERA] ERROR: Could not open camera")
        return None

    cap.set(cv2.CAP_PROP_FRAME_WIDTH,  CAPTURE_WIDTH)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, CAPTURE_HEIGHT)
    cap.set(cv2.CAP_PROP_FPS,          CAPTURE_FPS)

    actual_w = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    actual_h = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
    actual_fps = cap.get(cv2.CAP_PROP_FPS)

    print(f"[CAMERA] Initialized: {actual_w}x{actual_h} @ {actual_fps}fps")
    return cap


def capture_frame(cap):
    """
    Capture a single frame.
    Returns: numpy array (H, W, 3) BGR or None on error
    """
    ret, frame = cap.read()
    if not ret:
        print("[CAMERA] ERROR: Failed to capture frame")
        return None
    return frame


def save_frame(frame, prefix="frame"):
    """Save frame to disk with timestamp."""
    os.makedirs(SAVE_DIR, exist_ok=True)

    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S_%f")
    filename = f"{SAVE_DIR}/{prefix}_{timestamp}.jpg"
    cv2.imwrite(filename, frame)

    # Cleanup old files
    files = sorted(os.listdir(SAVE_DIR))
    while len(files) > MAX_SAVED:
        os.remove(f"{SAVE_DIR}/{files.pop(0)}")

    return filename


def process_frame(frame):
    """
    Placeholder for ML processing.
    Juan's model will go here.
    
    Returns: dict with detection results
    Example: {"obstacle_detected": True, "distance_cm": 45.0}
    """
    # TODO: integrate Juan's ML model here
    # result = juan_model.predict(frame)
    # return result

    # For now return dummy result
    return {
        "obstacle_detected": False,
        "distance_cm":       -1.0,
        "confidence":         0.0
    }


def run_camera_loop():
    """Main camera loop - captures and processes frames continuously."""
    print("[CAMERA] Starting camera loop...")

    cap = init_camera()
    if cap is None:
        return

    frame_count = 0
    start_time  = time.time()

    try:
        while True:
            frame = capture_frame(cap)
            if frame is None:
                time.sleep(0.1)
                continue

            # Process frame with ML model
            result = process_frame(frame)

            frame_count += 1

            # Print stats every 30 frames
            if frame_count % 30 == 0:
                elapsed = time.time() - start_time
                fps     = frame_count / elapsed
                print(f"[CAMERA] Frame {frame_count} | FPS: {fps:.1f} | "
                      f"Obstacle: {result['obstacle_detected']} | "
                      f"Distance: {result['distance_cm']:.1f} cm")

            # Save frame periodically for debugging
            if frame_count % 100 == 0:
                path = save_frame(frame)
                print(f"[CAMERA] Saved: {path}")

            time.sleep(1.0 / CAPTURE_FPS)

    except KeyboardInterrupt:
        print("\n[CAMERA] Stopped by user")
    finally:
        cap.release()
        print("[CAMERA] Released")


if __name__ == "__main__":
    run_camera_loop()