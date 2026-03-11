// CollabSync Signaling Server
// Simple TCP line protocol — no dependencies, just Node.js built-ins.
//
// Protocol:
//   Client → Server:  "JOIN <roomCode> <udpPort>\n"
//   Server → Client:  "PEER <ip> <udpPort>\n"   (sent when second peer joins)
//   Server → Client:  "WAIT\n"                   (sent when first peer joins, waiting)
//   Server → Client:  "ERROR <reason>\n"

const net  = require('net');
const os   = require('os');
const PORT = 8765;

// rooms: Map<roomCode, Array<{socket, udpPort}>>
const rooms = new Map();

// Get the first non-internal IPv4 address of this machine
function getLocalLanIp() {
    const interfaces = os.networkInterfaces();
    for (const name of Object.keys(interfaces)) {
        for (const iface of interfaces[name]) {
            if (iface.family === 'IPv4' && !iface.internal) {
                return iface.address;
            }
        }
    }
    return '127.0.0.1';
}

const serverLanIp = getLocalLanIp();

const server = net.createServer(socket => {
    socket.setEncoding('utf8');

    let buffer  = '';
    let myRoom  = null;
    let myPort  = null;

    socket.on('data', data => {
        buffer += data;
        const lines = buffer.split('\n');
        buffer = lines.pop(); // keep any incomplete trailing line

        for (const line of lines) {
            const parts = line.trim().split(' ');
            if (parts.length < 3 || parts[0] !== 'JOIN') continue;

            myRoom = parts[1].toUpperCase();
            myPort = parseInt(parts[2], 10);

            if (isNaN(myPort) || myPort < 1024 || myPort > 65535) {
                socket.write('ERROR invalid_port\n');
                continue;
            }

            if (!rooms.has(myRoom)) rooms.set(myRoom, []);
            const peers = rooms.get(myRoom);

            if (peers.length >= 2) {
                socket.write('ERROR room_full\n');
                continue;
            }

            // Tell all existing peers about the new arrival, and vice versa
            for (const peer of peers) {
                let peerIp = peer.socket.remoteAddress.replace('::ffff:', '');
                let myIp   = socket.remoteAddress.replace('::ffff:', '');

                // Replace loopback with actual LAN IP so peers on
                // different machines can reach the server's host
                if (peerIp === '127.0.0.1' || peerIp === '::1') peerIp = serverLanIp;
                if (myIp   === '127.0.0.1' || myIp   === '::1') myIp   = serverLanIp;

                socket.write(`PEER ${peerIp} ${peer.udpPort}\n`);
                peer.socket.write(`PEER ${myIp} ${myPort}\n`);
                console.log(`[${myRoom}] Paired: ${myIp}:${myPort} <-> ${peerIp}:${peer.udpPort}`);
            }

            if (peers.length === 0) {
                socket.write('WAIT\n');
                console.log(`[${myRoom}] Waiting for second peer...`);
            }

            peers.push({ socket, udpPort: myPort });
        }
    });

    socket.on('close', () => {
        if (!myRoom || !rooms.has(myRoom)) return;
        const peers = rooms.get(myRoom);
        const idx   = peers.findIndex(p => p.socket === socket);
        if (idx !== -1) peers.splice(idx, 1);
        if (peers.length === 0) rooms.delete(myRoom);
        console.log(`[${myRoom}] Peer disconnected. ${peers.length} remaining.`);
    });

    socket.on('error', () => socket.destroy());
});

server.listen(PORT, '0.0.0.0', () => {
    console.log(`CollabSync signaling server listening on port ${PORT}`);
});
