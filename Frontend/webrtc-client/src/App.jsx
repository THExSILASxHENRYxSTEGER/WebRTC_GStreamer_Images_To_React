import { useEffect, useRef, useState } from "react";

function useWebRTC(ws, viewerId, videoRef) {
  useEffect(() => {
    if (!ws) return;

    const pc = new RTCPeerConnection({
      iceServers: [
        {
          urls: "stun:stun.l.google.com:19302",
        },
      ],
    });

    console.log(`[${viewerId}] PeerConnection created`);

    pc.ontrack = (event) => {
      console.log(`[${viewerId}] TRACK received`);

      if (videoRef.current) {
        videoRef.current.srcObject = event.streams[0];
      }
    };

    pc.onconnectionstatechange = () => {
      console.log(
        `[${viewerId}] state =`,
        pc.connectionState
      );
    };

    pc.onicecandidate = (event) => {
      if (!event.candidate) return;

      console.log(`[${viewerId}] sending ICE`);

      ws.send(
        JSON.stringify({
          id: viewerId,
          type: "ice",
          candidate: event.candidate.candidate,
          sdpMLineIndex: event.candidate.sdpMLineIndex,
        })
      );
    };

    const handleMessage = async (event) => {
      const msg = JSON.parse(event.data);

      console.log(`[${viewerId}] RX`, msg);

      try {
        // OFFER → from backend (GStreamer)
        if (msg.type === "offer" && msg.target_id === viewerId) {
          console.log(`[${viewerId}] setting remote OFFER`);

          await pc.setRemoteDescription({
            type: "offer",
            sdp: msg.sdp,
          });

          const answer = await pc.createAnswer();
          await pc.setLocalDescription(answer);

          ws.send(
            JSON.stringify({
              id: viewerId,
              target_id: msg.id, // reply to sender
              type: "answer",
              sdp: answer.sdp,
            })
          );

          console.log(`[${viewerId}] ANSWER sent`);
        }

        // ANSWER → rarely needed here but safe
        if (msg.type === "answer" && msg.target_id === viewerId) {
          console.log(`[${viewerId}] setting remote ANSWER`);

          await pc.setRemoteDescription({
            type: "answer",
            sdp: msg.sdp,
          });
        }

        // ICE → both directions
        if (msg.type === "ice" && msg.target_id === viewerId) {
          console.log(`[${viewerId}] adding ICE`);

          await pc.addIceCandidate({
            candidate: msg.candidate,
            sdpMLineIndex: msg.sdpMLineIndex,
          });
        }
      } catch (err) {
        console.error(`[${viewerId}] WebRTC error`, err);
      }
    };

    ws.addEventListener("message", handleMessage);

    return () => {
      ws.removeEventListener("message", handleMessage);
      pc.close();
    };
  }, [ws, viewerId, videoRef]);
}

export default function App() {
  const [ws, setWs] = useState(null);

  const video100 = useRef(null);
  const video101 = useRef(null);

  useEffect(() => {
    const socket = new WebSocket("ws://localhost:8000/ws");

    socket.onopen = () => {
      console.log("WebSocket connected");

      // register both peers
      socket.send(JSON.stringify({ type: "register", id: 100 }));
      socket.send(JSON.stringify({ type: "register", id: 101 }));

      setWs(socket);
    };

    socket.onclose = () => {
      console.log("WebSocket closed");
    };

    socket.onerror = (e) => {
      console.error("WebSocket error", e);
    };

    return () => socket.close();
  }, []);

  useWebRTC(ws, 100, video100);
  useWebRTC(ws, 101, video101);

  return (
    <div style={{ padding: 20 }}>
      <h2>WebRTC Viewer</h2>

      <video
        ref={video100}
        autoPlay
        playsInline
        muted
        style={{ width: 800, background: "black", display: "block" }}
      />

      <video
        ref={video101}
        autoPlay
        playsInline
        muted
        style={{
          width: 800,
          background: "black",
          marginTop: 20,
        }}
      />
    </div>
  );
}