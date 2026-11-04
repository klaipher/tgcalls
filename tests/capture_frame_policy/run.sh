#!/bin/sh
exec "$(dirname "$0")/../run_policy_test.sh" capture_frame_policy CaptureFramePolicyTest.cpp
