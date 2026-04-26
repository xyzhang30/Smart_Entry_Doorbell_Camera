import React, { useEffect, useState, useCallback } from 'react';
import axios from 'axios';
import './StatsPage.css';

function StatsPage() {
  const [persons, setPersons] = useState([]);
  const [stats, setStats] = useState({ registered: {}, unregistered: 0 });
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState(null);
  const baseUrl = process.env.REACT_APP_BASE_URL;

  const fetchStats = useCallback(async () => {
    setLoading(true);
    setError(null);

    try {
      const [personsRes, logsRes] = await Promise.all([
        axios.get(`${baseUrl}/face/persons`),
        axios.get(`${baseUrl}/camera/get_logs`),
      ]);

      const personsList = personsRes.data;
      const counts = personsList.reduce((acc, person) => {
        acc[person.name] = 0;
        return acc;
      }, {});

      const logs = logsRes.data || [];
      let totalRegisteredSeen = 0;

      logs.forEach(log => {
        const names = log.recognized_names || [];
        names.forEach(name => {
          if (name !== 'Unknown' && Object.prototype.hasOwnProperty.call(counts, name)) {
            counts[name] += 1;
            totalRegisteredSeen += 1;
          }
        });
      });

      const unregisteredCount = Math.max(0, logs.length - totalRegisteredSeen);

      setPersons(personsList);
      setStats({ registered: counts, unregistered: unregisteredCount });
    } catch (err) {
      setError('Could not load recognition stats.');
    } finally {
      setLoading(false);
    }
  }, [baseUrl]);

  useEffect(() => {
    fetchStats();
  }, [fetchStats]);

  const renderRegisteredRows = () => {
    if (persons.length === 0) {
      return (
        <tr>
          <td colSpan="2" className="stats-empty">
            No registered faces yet.
          </td>
        </tr>
      );
    }

    const sortedPersons = [...persons].sort((a, b) => {
      const countA = stats.registered[a.name] ?? 0;
      const countB = stats.registered[b.name] ?? 0;
      if (countA !== countB) {
        return countB - countA;
      }
      return a.name.localeCompare(b.name);
    });

    return sortedPersons.map(person => (
      <tr key={person.id}>
        <td>{person.name}</td>
        <td>{stats.registered[person.name] ?? 0}</td>
      </tr>
    ));
  };

  const registeredSightings = Object.values(stats.registered).reduce((sum, value) => sum + value, 0);

  return (
    <div className="stats-container">
      <div className="stats-header">
        <div>
          <h1>Recognition Stats</h1>
          <p>Track how often registered and unregistered people are seen.</p>
        </div>
        <button className="stats-refresh" onClick={fetchStats} disabled={loading}>
          {loading ? 'Refreshing…' : 'Refresh'}
        </button>
      </div>

      {error && <p className="stats-error">{error}</p>}

      <div className="stats-summary">
        <div className="stats-card">
          <span className="stats-card-label">Unregistered seen</span>
          <span className="stats-card-value">{stats.unregistered}</span>
        </div>
        <div className="stats-card stats-card--secondary">
          <span className="stats-card-label">Registered sightings</span>
          <span className="stats-card-value">{registeredSightings}</span>
        </div>
      </div>

      <section className="stats-table-section">
        <h2>Registered person sighting counts</h2>
        <div className="stats-table-wrapper">
          <table className="stats-table">
            <thead>
              <tr>
                <th>Name</th>
                <th>Times seen</th>
              </tr>
            </thead>
            <tbody>
              {loading ? (
                <tr>
                  <td colSpan="2" className="stats-loading">
                    Loading stats…
                  </td>
                </tr>
              ) : (
                renderRegisteredRows()
              )}
            </tbody>
          </table>
        </div>
      </section>
    </div>
  );
}

export default StatsPage;
