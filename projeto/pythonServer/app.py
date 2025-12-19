from flask import Flask, request, render_template, jsonify
from iv_monitor import IVMonitor

app = Flask(__name__)

monitor = None

@app.route('/')
def index():
    return render_template('index.html')

@app.route('/configure', methods=['POST'])
def configure():
    global monitor
    data = request.json
    try:
        vol = float(data.get('volume', 500))
        factor = float(data.get('factor', 20))
        monitor = IVMonitor(total_volume_ml=vol, dripping_factor=factor)
        return jsonify({"status": "success", "message": "Monitoring started"})
    except ValueError:
        return jsonify({"status": "error", "message": "Invalid values"}), 400

@app.route('/reset', methods=['POST'])
def reset():
    global monitor
    monitor = None
    return jsonify({"status": "reset"})

@app.route('/send', methods=['POST'])
def receive_data_from_esp():
    global monitor
    if monitor is None:
        return "Not Configured", 200
        
    try:
        monitor.register_drip()
        return "OK", 200
    except:
        return "Error", 400

@app.route('/status')
def status():
    global monitor
    if monitor is None:
        return jsonify({"configured": False})
    
    stats = monitor.calculate_values()
    if stats:
        stats["configured"] = True
        return jsonify(stats)
    else:
        return jsonify({"configured": True, "waiting": True})

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, debug=True)