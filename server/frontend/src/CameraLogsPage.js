import React, { useEffect, useState } from 'react';
import axios from 'axios';
import './CameraLogsPage.css';

function CameraLogsPage() {
  const [logs, setLogs] = useState([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState(null);
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

  return (
    <div className="camera-logs-container">
      <h1>Camera Logs</h1>

      {loading && <p className="camera-logs-loading">Loading...</p>}
      {error && <p className="camera-logs-error">{error}</p>}

      {!loading && !error && (
        <>
          {logs.length === 0 ? (
            <p className="camera-logs-empty">No camera logs yet.</p>
          ) : (
            <div className="camera-logs-timeline">
              {logs.map((log) => (
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
                </div>
              ))}
            </div>
          )}
        </>
      )}
    </div>
  );
}

export default CameraLogsPage;