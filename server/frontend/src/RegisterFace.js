import React, { useState, useEffect } from 'react';
import './RegisterFace.css';

function RegisterFace() {
  const [username, setUsername] = useState('');
  const [image, setImage] = useState(null);
  const [preview, setPreview] = useState(null);
  const [loading, setLoading] = useState(false);
  const [message, setMessage] = useState('');
  const [messageType, setMessageType] = useState(''); // 'success', 'error', 'info'
  const [registeredUsers, setRegisteredUsers] = useState([]);
  const [activeTab, setActiveTab] = useState('register'); // 'register' or 'view'

  // Fetch registered users
  useEffect(() => {
    if (activeTab === 'view') {
      fetchRegisteredUsers();
    }
  }, [activeTab]);

  const fetchRegisteredUsers = async () => {
    try {
      const response = await fetch('http://localhost:5000/user/list');
      const users = await response.json();
      setRegisteredUsers(users);
    } catch (error) {
      console.error('Error fetching users:', error);
      setMessage('Failed to fetch registered users');
      setMessageType('error');
    }
  };

  const handleImageSelect = (e) => {
    const file = e.target.files[0];
    if (file) {
      setImage(file);
      // Create preview
      const reader = new FileReader();
      reader.onload = (e) => {
        setPreview(e.target.result);
      };
      reader.readAsDataURL(file);
    }
  };

  const handleSubmit = async (e) => {
    e.preventDefault();
    
    if (!username.trim()) {
      setMessage('Please enter a username');
      setMessageType('error');
      return;
    }

    if (!image) {
      setMessage('Please select an image');
      setMessageType('error');
      return;
    }

    setLoading(true);
    setMessage('');

    const formData = new FormData();
    formData.append('username', username);
    formData.append('image', image);

    try {
      const response = await fetch('http://localhost:5000/user/upload_face', {
        method: 'POST',
        body: formData,
      });

      const data = await response.json();

      if (response.ok) {
        setMessage(`✓ Face registered for ${username}!`);
        setMessageType('success');
        setUsername('');
        setImage(null);
        setPreview(null);
        // Refresh user list
        fetchRegisteredUsers();
      } else {
        if (data.error === 'face_detection_failed') {
          setMessage(`Face detection failed: ${data.msg}. Make sure the image has a clear, well-lit face.`);
        } else {
          setMessage(data.msg || 'Error uploading face');
        }
        setMessageType('error');
      }
    } catch (error) {
      console.error('Error:', error);
      setMessage('Network error. Make sure the backend is running.');
      setMessageType('error');
    } finally {
      setLoading(false);
    }
  };

  const handleDeleteUser = async (userId) => {
    if (window.confirm('Delete this user?')) {
      try {
        const response = await fetch(`http://localhost:5000/user/${userId}`, {
          method: 'DELETE',
        });

        if (response.ok) {
          setMessage('User deleted');
          setMessageType('success');
          fetchRegisteredUsers();
        } else {
          setMessage('Failed to delete user');
          setMessageType('error');
        }
      } catch (error) {
        console.error('Error:', error);
        setMessage('Network error');
        setMessageType('error');
      }
    }
  };

  return (
    <div className="register-face-container">
      <div className="register-face-box">
        <h1>Facial Recognition Setup</h1>
        
        {/* Tabs */}
        <div className="tabs">
          <button
            className={`tab-button ${activeTab === 'register' ? 'active' : ''}`}
            onClick={() => setActiveTab('register')}
          >
            Register Face
          </button>
          <button
            className={`tab-button ${activeTab === 'view' ? 'active' : ''}`}
            onClick={() => setActiveTab('view')}
          >
            Registered Users
          </button>
        </div>

        {/* Message Alert */}
        {message && (
          <div className={`alert alert-${messageType}`}>
            {message}
          </div>
        )}

        {/* Register Tab */}
        {activeTab === 'register' && (
          <div className="register-tab">
            <p className="info-text">
              Upload a clear, front-facing photo with good lighting to register your face for doorbell recognition.
            </p>

            <form onSubmit={handleSubmit} className="register-form">
              <div className="form-group">
                <label htmlFor="username">Username:</label>
                <input
                  id="username"
                  type="text"
                  placeholder="Enter your name"
                  value={username}
                  onChange={(e) => setUsername(e.target.value)}
                  disabled={loading}
                />
              </div>

              <div className="form-group">
                <label htmlFor="image">Photo:</label>
                <div className="image-upload-box">
                  <input
                    id="image"
                    type="file"
                    accept="image/jpeg,image/jpg,image/png,image/gif"
                    onChange={handleImageSelect}
                    disabled={loading}
                    style={{ display: 'none' }}
                  />
                  <label htmlFor="image" className="file-input-label">
                    {preview ? 'Change Photo' : 'Choose Photo'}
                  </label>
                  {preview && (
                    <div className="preview-container">
                      <img src={preview} alt="Preview" className="preview-image" />
                    </div>
                  )}
                </div>
                <small>Supported formats: JPG, PNG, GIF</small>
              </div>

              <button
                type="submit"
                disabled={loading || !username || !image}
                className="submit-button"
              >
                {loading ? 'Registering...' : 'Register Face'}
              </button>
            </form>
          </div>
        )}

        {/* View Tab */}
        {activeTab === 'view' && (
          <div className="view-tab">
            {registeredUsers.length === 0 ? (
              <p className="no-users">No users registered yet</p>
            ) : (
              <div className="users-list">
                {registeredUsers.map((user) => (
                  <div key={user.id} className="user-card">
                    <div className="user-info">
                      <h3>{user.username}</h3>
                      <p className={`face-status ${user.has_face ? 'registered' : 'pending'}`}>
                        {user.has_face ? '✓ Face Registered' : '○ No Face'}
                      </p>
                    </div>
                    {user.profile_image && user.has_face && (
                      <div className="user-preview">
                        <img src={user.profile_image} alt={user.username} />
                      </div>
                    )}
                    <button
                      className="delete-button"
                      onClick={() => handleDeleteUser(user.id)}
                    >
                      Delete
                    </button>
                  </div>
                ))}
              </div>
            )}
          </div>
        )}
      </div>
    </div>
  );
}

export default RegisterFace;
