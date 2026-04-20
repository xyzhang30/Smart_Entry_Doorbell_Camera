import './App.css';
import { useState } from 'react';
import CameraLogsPage from './CameraLogsPage';
import RegisterFace from './RegisterFace';

function App() {
  const [currentPage, setCurrentPage] = useState('logs'); // 'logs' or 'register'

  return (
    <div className="app">
      {/* Navigation */}
      <nav className="app-nav">
        <div className="nav-container">
          <h2 className="app-title">Smart Doorbell Camera</h2>
          <ul className="nav-links">
            <li>
              <button 
                className={`nav-button ${currentPage === 'logs' ? 'active' : ''}`}
                onClick={() => setCurrentPage('logs')}
              >
                Camera Logs
              </button>
            </li>
            <li>
              <button 
                className={`nav-button ${currentPage === 'register' ? 'active' : ''}`}
                onClick={() => setCurrentPage('register')}
              >
                Register Face
              </button>
            </li>
          </ul>
        </div>
      </nav>

      {/* Page Content */}
      <main className="app-content">
        {currentPage === 'logs' && <CameraLogsPage />}
        {currentPage === 'register' && <RegisterFace />}
      </main>
    </div>
  );
}

export default App;

