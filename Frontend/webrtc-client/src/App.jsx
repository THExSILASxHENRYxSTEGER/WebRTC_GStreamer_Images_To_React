import { useEffect, useRef, useState } from "react";

function VideoStream({ cppId, browserId, ws }) {
  const videoRef     = useRef(null);
  const pcRef         = useRef(null);
  const streamRef      = useRef(null);
  const pendingIceRef  = useRef([]);   // ICE candidates that arrive before remote desc is set
  const remoteSetRef   = useRef(false);

  const setVideoRef = (el) => {
    videoRef.current = el;
    if (el && streamRef.current) {
      el.srcObject = streamRef.current;
    }
  };

  useEffect(() => {
    if (!ws) return;

    if (pcRef.current) { pcRef.current.close(); pcRef.current = null; }
    remoteSetRef.current = false;
    pendingIceRef.current = [];

    const pc = new RTCPeerConnection({
      iceServers: [{ urls: "stun:stun.l.google.com:19302" }],
    });
    pcRef.current = pc;

    pc.ontrack = (event) => {
      console.log(`[${browserId}] TRACK kind=${event.track.kind}`);
      const stream = event.streams[0];
      streamRef.current = stream;
      if (videoRef.current) {
        videoRef.current.srcObject = stream;
      }
    };

    pc.onicecandidate = (event) => {
      if (!event.candidate) return;
      console.log(`[${browserId}] candidate type:`, event.candidate.type, event.candidate.candidate);
      ws.send(JSON.stringify({
        type: "ice",
        id: browserId,
        target_id: cppId,
        candidate: event.candidate.candidate,
        sdpMLineIndex: event.candidate.sdpMLineIndex,
      }));
    };

    pc.onconnectionstatechange = () =>
      console.log(`[${browserId}] conn: ${pc.connectionState}`);

    pc.oniceconnectionstatechange = async () => {
      console.log(`[${browserId}] ICE: ${pc.iceConnectionState}`);
      if (pc.iceConnectionState === "failed" || pc.iceConnectionState === "disconnected") {
        const stats = await pc.getStats();
        stats.forEach(report => {
          if (report.type === "candidate-pair" && report.state) {
            console.log(`[${browserId}] candidate-pair state=${report.state}`, report);
          }
        });
      }
    };

    const flushPendingIce = async () => {
      const queued = pendingIceRef.current;
      pendingIceRef.current = [];
      for (const cand of queued) {
        try {
          await pc.addIceCandidate(cand);
        } catch (e) {
          console.error(`[${browserId}] flush ICE error:`, e);
        }
      }
    };

    const handleMessage = async (event) => {
      let msg;
      try { msg = JSON.parse(event.data); } catch { return; }
      if (msg.target_id !== browserId) return;

      try {
        if (msg.type === "offer") {
          await pc.setRemoteDescription(
            new RTCSessionDescription({ type: "offer", sdp: msg.sdp })
          );
          remoteSetRef.current = true;
          await flushPendingIce();

          const answer = await pc.createAnswer();
          await pc.setLocalDescription(answer);
          ws.send(JSON.stringify({
            type: "answer", id: browserId, target_id: cppId, sdp: answer.sdp,
          }));
          console.log(`[${browserId}] answer sent`);
        }

        if (msg.type === "ice" && msg.candidate) {
          const cand = new RTCIceCandidate({
            candidate: msg.candidate,
            sdpMLineIndex: msg.sdpMLineIndex,
          });
          if (remoteSetRef.current) {
            await pc.addIceCandidate(cand);
          } else {
            // remote description not set yet — queue it
            pendingIceRef.current.push(cand);
          }
        }
      } catch (e) {
        console.error(`[${browserId}] error:`, e);
      }
    };

    ws.addEventListener("message", handleMessage);
    return () => {
      ws.removeEventListener("message", handleMessage);
      pc.close();
      pcRef.current = null;
    };
  }, [ws]);

  return (
    <div>
      <p style={{ margin: "0 0 4px" }}>Stream {cppId}</p>
      <video
        ref={setVideoRef}
        autoPlay
        playsInline
        muted
        style={{ width: 640, height: 360, background: "#000", display: "block" }}
      />
    </div>
  );
}

export default function App() {
  const [ws, setWs] = useState(null);
  const socketRef    = useRef(null);

  useEffect(() => {
    if (socketRef.current) return;

    const socket = new WebSocket("ws://localhost:8000/ws");
    socketRef.current = socket;

    socket.onopen = () => {
      console.log("WS open");
      socket.send(JSON.stringify({ type: "register", id: 200 }));
      socket.send(JSON.stringify({ type: "register", id: 201 }));
      setWs(socket);
    };

    socket.onerror = (e) => console.error("WS error", e);
    socket.onclose = () => {
      console.log("WS closed");
      socketRef.current = null;
      setWs(null);
    };

    return () => {
      socket.close();
      socketRef.current = null;
    };
  }, []);

  return (
    <div style={{ fontFamily: "sans-serif", padding: 20 }}>
      <h2>WebRTC Viewer</h2>
      <div style={{ display: "flex", gap: 16 }}>
        <VideoStream ws={ws} cppId={100} browserId={200} />
        <VideoStream ws={ws} cppId={101} browserId={201} />
      </div>
    </div>
  );
}