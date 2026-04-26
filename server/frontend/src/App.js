import './App.css';
import { useState } from 'react';
import CameraLogsPage from './CameraLogsPage';
import FacesPage from './Facepage';
import StatsPage from './StatsPage';

function App() {
  const [tab, setTab] = useState('logs');

  return (
    <div>
      <nav className="app-nav">
        <button
          className={`app-nav-tab ${tab === 'logs' ? 'app-nav-tab--active' : ''}`}
          onClick={() => setTab('logs')}
        >
          Camera Logs
        </button>
        <button
          className={`app-nav-tab ${tab === 'stats' ? 'app-nav-tab--active' : ''}`}
          onClick={() => setTab('stats')}
        >
          Stats
        </button>
        <button
          className={`app-nav-tab ${tab === 'faces' ? 'app-nav-tab--active' : ''}`}
          onClick={() => setTab('faces')}
        >
          Manage Faces
        </button>
      </nav>

      {tab === 'logs' ? <CameraLogsPage /> : tab === 'stats' ? <StatsPage /> : <FacesPage />}
    </div>
  );
}

export default App;