// Message types matching server
const MSG_TYPE = {
    HANDSHAKE: 0,
    READY: 1,
    AUTH: 2,
    ERROR: 3,
    DISCONNECT: 4,
    KEEPALIVE: 5,
    STATE_SYNC: 6,
    DELTA_UPDATE: 7,
    AUTH_PENDING: 8
};

class GameClient {
    constructor() {
        this.socket = null;
        this.connected = false;
        this.authenticated = false;
        this.retryCount = 0;
        this.maxRetries = 3;
        this.authTimeout = null;
        
        // Binary message handling
        this.decoder = new TextDecoder();
        this.encoder = new TextEncoder();
    }

    async connect() {
        try {
            this.updateStatus('Connecting...');
            this.socket = new WebSocket('ws://localhost:8080');
            
            this.socket.binaryType = 'arraybuffer';
            
            this.socket.onopen = () => this.handleConnect();
            this.socket.onmessage = (event) => this.handleMessage(event.data);
            this.socket.onclose = () => this.handleDisconnect();
            this.socket.onerror = (error) => this.handleError(error);
            
        } catch (error) {
            this.updateStatus('Connection failed: ' + error);
            this.retryConnection();
        }
    }

    handleConnect() {
        this.connected = true;
        this.updateStatus('Connected, waiting for handshake...');
    }

    async handleMessage(data) {
        const view = new DataView(data);
        const messageType = view.getUint8(0);

        switch (messageType) {
            case MSG_TYPE.HANDSHAKE:
                this.handleHandshake(view);
                break;

            case MSG_TYPE.AUTH_PENDING:
                const timeout = view.getUint32(1);
                this.handleAuthPending(timeout);
                break;

            case MSG_TYPE.READY:
                this.handleReady(view);
                break;

            case MSG_TYPE.ERROR:
                this.handleError(view);
                break;

            // ... other message types
        }
    }

    handleHandshake(view) {
        this.updateStatus('Handshake received, authenticating...');
        
        // Get auth token (from localStorage, OAuth, etc)
        const token = this.getAuthToken();
        
        // Send auth message
        const authMsg = new ArrayBuffer(64); // Adjust size based on your AuthMessage struct
        const authView = new DataView(authMsg);
        authView.setUint8(0, MSG_TYPE.AUTH);
        // Fill in rest of auth message...
        
        this.socket.send(authMsg);
    }

    handleAuthPending(timeout) {
        this.updateStatus('Auth pending, waiting...');
        
        // Clear any existing timeout
        if (this.authTimeout) {
            clearTimeout(this.authTimeout);
        }

        // Set new timeout
        this.authTimeout = setTimeout(() => {
            if (!this.authenticated) {
                this.retryAuth();
            }
        }, timeout);
    }

    handleReady(view) {
        this.authenticated = true;
        this.updateStatus('Authentication successful!');
        // Start game loop...
    }

    handleError() {
        if (!this.authenticated) {
            this.retryAuth();
        }
    }

    retryAuth() {
        if (this.retryCount < this.maxRetries) {
            this.retryCount++;
            this.updateStatus(`Retrying authentication (${this.retryCount}/${this.maxRetries})...`);
            // Resend auth message...
        } else {
            this.updateStatus('Authentication failed after max retries');
            this.socket.close();
        }
    }

    handleDisconnect() {
        this.connected = false;
        this.authenticated = false;
        this.updateStatus('Disconnected');
        
        if (this.authTimeout) {
            clearTimeout(this.authTimeout);
        }

        // Optional: attempt reconnection after delay
        setTimeout(() => this.connect(), 5000);
    }

    updateStatus(message) {
        const status = document.getElementById('status');
        if (status) {
            status.textContent = message;
        }
        console.log(message);
    }

    // Helper to get auth token - implement based on your auth system
    getAuthToken() {
        return localStorage.getItem('authToken') || 'temp-token';
    }
}

// Initialize and connect
document.getElementById('connect').addEventListener('click', () => {
    const client = new GameClient();
    client.connect();
});
