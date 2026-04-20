import React, { useEffect, useState, useRef, useCallback } from 'react';
import axios from 'axios';
import './Facepage.css';

function FacesPage() {
  const [persons, setPersons] = useState([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState(null);
  const [name, setName] = useState('');
  const [imageFile, setImageFile] = useState(null);
  const [submitting, setSubmitting] = useState(false);
  const [feedback, setFeedback] = useState(null); // { type: 'success'|'error', msg }
  const fileInputRef = useRef(null);
  const baseUrl = process.env.REACT_APP_BASE_URL;

  const fetchPersons = useCallback(async () => {
    try {
      const res = await axios.get(`${baseUrl}/face/persons`);
      setPersons(res.data);
    } catch (err) {
      setError('Could not load registered faces.');
    } finally {
      setLoading(false);
    }
  }, [baseUrl]);

  useEffect(() => { fetchPersons(); }, [fetchPersons]);

  async function handleSubmit(e) {
    e.preventDefault();
    if (!name.trim() || !imageFile) {
      setFeedback({ type: 'error', msg: 'Name and image are both required.' });
      return;
    }
    setSubmitting(true);
    setFeedback(null);
    const formData = new FormData();
    formData.append('name', name.trim());
    formData.append('image', imageFile);
    try {
      const res = await axios.post(`${baseUrl}/face/add_person`, formData, {
        headers: { 'Content-Type': 'multipart/form-data' }
      });
      setFeedback({ type: 'success', msg: res.data.msg });
      setName('');
      setImageFile(null);
      if (fileInputRef.current) fileInputRef.current.value = '';
      fetchPersons();
    } catch (err) {
      const msg = err.response?.data?.msg || 'Upload failed.';
      setFeedback({ type: 'error', msg });
    } finally {
      setSubmitting(false);
    }
  }

  async function handleDelete(id, personName) {
    if (!window.confirm(`Remove "${personName}"?`)) return;
    try {
      await axios.delete(`${baseUrl}/face/persons/${id}`);
      setPersons(prev => prev.filter(p => p.id !== id));
    } catch {
      setFeedback({ type: 'error', msg: 'Delete failed.' });
    }
  }

  return (
    <div className="faces-container">
      <h1>Manage Faces</h1>

      <section className="faces-section">
        <h2>Register a face</h2>
        <form className="faces-form" onSubmit={handleSubmit}>
          <div className="faces-field">
            <label htmlFor="face-name">Name</label>
            <input
              id="face-name"
              type="text"
              placeholder="e.g. Alice"
              value={name}
              onChange={e => setName(e.target.value)}
              disabled={submitting}
            />
          </div>
          <div className="faces-field">
            <label htmlFor="face-image">Reference photo</label>
            <input
              id="face-image"
              type="file"
              accept="image/*"
              ref={fileInputRef}
              onChange={e => setImageFile(e.target.files[0] || null)}
              disabled={submitting}
            />
          </div>
          <button type="submit" className="faces-submit" disabled={submitting}>
            {submitting ? 'Uploading…' : 'Add person'}
          </button>
        </form>

        {feedback && (
          <p className={`faces-feedback faces-feedback--${feedback.type}`}>
            {feedback.msg}
          </p>
        )}
      </section>

      <section className="faces-section">
        <h2>Registered faces</h2>
        {loading && <p className="faces-empty">Loading…</p>}
        {error && <p className="faces-feedback faces-feedback--error">{error}</p>}
        {!loading && !error && persons.length === 0 && (
          <p className="faces-empty">No faces registered yet.</p>
        )}
        {!loading && persons.length > 0 && (
          <ul className="faces-list">
            {persons.map(p => (
              <li key={p.id} className="faces-list-item">
                <span className="faces-list-name">{p.name}</span>
                <button
                  className="faces-delete"
                  onClick={() => handleDelete(p.id, p.name)}
                  aria-label={`Remove ${p.name}`}
                >
                  Remove
                </button>
              </li>
            ))}
          </ul>
        )}
      </section>
    </div>
  );
}

export default FacesPage;