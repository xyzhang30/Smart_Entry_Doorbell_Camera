import React, { useEffect, useState } from 'react';
import axios from 'axios';
import './CameraLogsPage.css';

function CameraLogsPage() {
  const [logs, setLogs] = useState([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState(null);
  // Map of log.id -> string[] of recognized names
  const [recognitions, setRecognitions] = useState({});
  const [recognizing, setRecognizing] = useState(false);
  const baseUrl = process.env.REACT_APP_BASE_URL;

  useEffect(() => {
    async function fetchLogs() {
      try {
        const response = await axios.get(`${baseUrl}/camera/get_logs`);
        const sortedLogs = response.data.sort(
          (a, b) => new Date(b.timestamp) - new Date(a.timestamp)
        );
        setLogs(sortedLogs);
      } catch (err) {
        const message = err.response?.data?.msg || err.message || 'Unable to load camera logs.';
        setError(message);
      } finally {
        setLoading(false);
      }
    }

    fetchLogs();
  }, []);

  async function handleRecognizeAll() {
    setRecognizing(true);
    try {
      const res = await axios.post(`${baseUrl}/face/recognize_all`);
      // results is { [log_id]: string[] }
      setRecognitions(res.data.results || {});
    } catch (err) {
      const message = err.response?.data?.msg || 'Recognition failed.';
      setError(message);
    } finally {
      setRecognizing(false);
    }
  }

  return (
    <div className="camera-logs-container">
      <div className="camera-logs-header">
        <h1>Camera Logs</h1>
        {!loading && !error && logs.length > 0 && (
          <button
            className="camera-logs-recognize-btn"
            onClick={handleRecognizeAll}
            disabled={recognizing}
          >
            {recognizing ? 'Recognizing…' : 'Run recognition'}
          </button>
        )}
      </div>

      {loading && <p className="camera-logs-loading">Loading...</p>}
      {error && <p className="camera-logs-error">{error}</p>}

      {!loading && !error && (
        <>
          {logs.length === 0 ? (
            <p className="camera-logs-empty">No camera logs yet.</p>
          ) : (
            <div className="camera-logs-timeline">
              {logs.map((log) => {
                const names = recognitions[log.id];
                return (
                  <div key={log.id} className="camera-logs-entry">
                    <div className="camera-logs-timestamp">
                      {new Date(log.timestamp).toLocaleString()}
                    </div>
                    <div className="camera-logs-image-container">
                      <img
                        src={`${baseUrl}/camera/images/${log.image.split('/').pop()}`}
                        alt={`Camera capture ${log.id}`}
                        className="camera-logs-image"
                      />
                    </div>
                    {names && names.length > 0 && (
                      <div className="camera-logs-names">
                        {names.map((n, i) => (
                          <span
                            key={i}
                            className={`camera-logs-name-badge ${n === 'Unknown' ? 'camera-logs-name-badge--unknown' : ''}`}
                          >
                            {n}
                          </span>
                        ))}
                      </div>
                    )}
                    {names && names.length === 0 && (
                      <div className="camera-logs-names">
                        <span className="camera-logs-name-badge camera-logs-name-badge--none">
                          No faces detected
                        </span>
                      </div>
                    )}
                  </div>
                );
              })}
            </div>
          )}
        </>
      )}
    </div>
  );
}

export default CameraLogsPage;