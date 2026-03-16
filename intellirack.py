import streamlit as st
import cv2
import mediapipe as mp
import numpy as np
import time

from streamlit_webrtc import webrtc_streamer, VideoProcessorBase

st.set_page_config(layout="wide")

st.title("🧠 IntelliRack Smart Shelf Analytics")

# ----------------------------
# Metrics
# ----------------------------

if "footfall" not in st.session_state:
    st.session_state.footfall = 0

if "pickup" not in st.session_state:
    st.session_state.pickup = 0

if "interest" not in st.session_state:
    st.session_state.interest = 0

if "dwell_start" not in st.session_state:
    st.session_state.dwell_start = None


# ----------------------------
# MediaPipe Init
# ----------------------------

mp_hands = mp.solutions.hands
mp_draw = mp.solutions.drawing_utils


# ----------------------------
# Video Processor
# ----------------------------

class IntelliRackProcessor(VideoProcessorBase):

    def __init__(self):

        self.hands = mp_hands.Hands(
            max_num_hands=2,
            min_detection_confidence=0.6,
            min_tracking_confidence=0.6
        )

        self.last_hand = False
        self.hand_frames = 0
        self.motion_bg = None


    def detect_motion(self, frame):

        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
        blur = cv2.GaussianBlur(gray,(21,21),0)

        if self.motion_bg is None:
            self.motion_bg = blur
            return False

        diff = cv2.absdiff(self.motion_bg, blur)
        thresh = cv2.threshold(diff,25,255,cv2.THRESH_BINARY)[1]

        movement = np.sum(thresh)

        return movement > 400000


    def recv(self, frame):

        img = frame.to_ndarray(format="bgr24")

        rgb = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)

        results = self.hands.process(rgb)

        # ----------------
        # Footfall Detection
        # ----------------

        if self.detect_motion(img):

            if st.session_state.dwell_start is None:

                st.session_state.footfall += 1
                st.session_state.dwell_start = time.time()


        # ----------------
        # Dwell Time
        # ----------------

        if st.session_state.dwell_start is not None:

            dwell = time.time() - st.session_state.dwell_start

            cv2.putText(
                img,
                f"Dwell {int(dwell)}s",
                (20,40),
                cv2.FONT_HERSHEY_SIMPLEX,
                1,
                (0,255,0),
                2
            )

            if dwell > 10:

                st.session_state.interest += 1
                st.session_state.dwell_start = None


        # ----------------
        # Hand Detection
        # ----------------

        hand_detected = False

        if results.multi_hand_landmarks:

            hand_detected = True

            for hand_landmarks in results.multi_hand_landmarks:

                mp_draw.draw_landmarks(
                    img,
                    hand_landmarks,
                    mp_hands.HAND_CONNECTIONS
                )


        # ----------------
        # Pickup Logic
        # ----------------

        if hand_detected:

            self.hand_frames += 1

        else:

            self.hand_frames = 0


        if self.hand_frames > 15:

            st.session_state.pickup += 1
            st.session_state.interest += 1
            self.hand_frames = 0


        return img


# ----------------------------
# Layout
# ----------------------------

col1, col2 = st.columns([3,1])

with col1:

    webrtc_streamer(
        key="rack",
        video_processor_factory=IntelliRackProcessor,
        media_stream_constraints={"video": True, "audio": False}
    )


with col2:

    st.subheader("📊 Live Metrics")

    st.metric("Footfall", st.session_state.footfall)
    st.metric("Pickup", st.session_state.pickup)
    st.metric("Interest", st.session_state.interest)