r"""Local dashboard fixture, never connects to the lift. Uses the project's PyYAML.

Run: venv\Scripts\python tests\preview_dashboard.py
Open http://127.0.0.1:8767/; select ?scene=normal|moving|fault|emergency|unknown|port|valves.
GET /test/requests reports requests sent by the browser. /test/drop closes SSE.
"""
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler
from pathlib import Path
from urllib.parse import urlsplit, parse_qs, unquote
from queue import Queue, Empty
import json
import threading
import time
import yaml

ROOT = Path(__file__).resolve().parents[1]
config = yaml.load((ROOT / 'boat-lift.yaml').read_text(encoding='utf-8'), Loader=yaml.BaseLoader)
substitutions = config['substitutions']
entities, requests, clients = {}, [], []
lock = threading.RLock()
connected = True
slow_response = False


def substitute(value):
    for key, replacement in substitutions.items():
        value = value.replace('${' + key + '}', replacement)
    return value


for domain in ('button', 'switch', 'number', 'binary_sensor', 'sensor', 'text_sensor'):
    for index, entry in enumerate(config.get(domain, [])):
        if 'name' not in entry or entry.get('internal') == 'true':
            continue
        name = substitute(entry['name'])
        data = dict(id=f'{domain}-fixture_{index}', name=name, domain=domain, name_id=f'{domain}/{name}')
        if 'unit_of_measurement' in entry:
            data['uom'] = entry['unit_of_measurement']
        if domain == 'number':
            for key in ('min_value', 'max_value', 'step'):
                data[key] = entry[key]
            data['value'] = str(entry.get('initial_value', '0'))
        elif domain in ('switch', 'binary_sensor'):
            data['value'] = False
        elif domain == 'sensor':
            data['value'] = None
        else:
            data['value'] = '—'
        value = data['value']
        data['state'] = ('ON' if value else 'OFF') if isinstance(value, bool) else 'NA' if value is None else str(value)
        entities[(domain, name)] = data


def update(domain, name, value):
    data = entities[(domain, name)]
    data['value'] = value
    data['state'] = ('ON' if value else 'OFF') if isinstance(value, bool) else 'NA' if value is None else str(value)
    # Live updates intentionally omit name/domain metadata, like ESPHome DETAIL_STATE.
    delta = {key: data[key] for key in ('id', 'name_id', 'value', 'state')}
    for client in clients:
        client.put(delta)


def scene(name):
    global connected, slow_response
    connected = True
    slow_response = name == 'slow'
    values = {
        'text_sensor': {'Lift Status': 'Lifted', 'Lift Activity': 'Idle', 'Lift Position': 'Lifted',
                        'Level Status': 'Level OK — Port 0.4% low',
                        'Valve Positions': 'Starboard CLOSED · Port CLOSED',
                        'Boat Load State': 'Confirmed Cobalt', 'Last Stop Reason': 'Lift target reached',
                        'Maintain Observe': 'At Lift — 2 top-ups, 1 level fixes — sag -0.30%/h',
                        'Calibration Summary': 'Lowered: >48.0°  Guide: 46.0°  Ready: 42.2°  Lift: -5.4°  Empty: -23.9°',
                        'Air Loss Detail': 'none'},
        'sensor': {'Lift Height': 97, 'Height Port': 96.6, 'Level Error': -0.4,
                   'Bunk Height': 28.3, 'Lift Water Temperature': 21.4, 'Uptime': 94200,
                   'Visit Height Top-ups': 2, 'Visit Level Events': 1},
        'binary_sensor': {'Lift Problem': False, 'Air Loss Alert': False, 'IMU Starboard OK': True, 'IMU Port OK': True},
        'switch': {'Auto-Maintain Height': True, 'Auto-Maintain Level': True, 'Bench Test (FSM off)': False,
                   'Bypass Mode': False, 'Blower Relay (Y1) — bench only': False},
    }
    if name == 'moving':
        values['text_sensor'].update({'Lift Status':'Raising -> Lift','Lift Activity':'Raising','Lift Position':'Between',
                                      'Valve Positions':'Starboard OPEN · Port OPEN'})
        values['sensor']['Lift Height'] = 46
        values['switch']['Blower Relay (Y1) — bench only'] = True
        values['text_sensor']['Level Status'] = 'Leveling — holding Starboard back'
    if name in ('fault', 'emergency'):
        values['text_sensor'].update({'Lift Status': 'FAULT — stall_no_progress' if name == 'fault' else 'EMERGENCY — descending to Ready (level failure)',
                                      'Lift Activity': 'Fault' if name == 'fault' else 'Emergency Descent'})
        values['binary_sensor']['Lift Problem'] = True
    if name == 'unknown':
        values['text_sensor'].update({'Lift Status':'Angle sensors OFFLINE - manual control','Lift Position':'Unknown',
                                      'Boat Load State':'Unknown','Level Status':'Port IMU offline — ganged'})
        values['sensor'].update({'Lift Height':None,'Height Port':None,'Level Error':None,'Bunk Height':None})
        values['binary_sensor'].update({'IMU Port OK':False,'IMU Starboard OK':False,'Lift Problem':True})
    if name == 'port':
        values['text_sensor'].update({'Lift Status':'Lifted — port angle','Lift Position':'Lifted',
                                      'Level Status':'Starboard IMU offline — height from Port, ganged'})
        values['binary_sensor'].update({'IMU Starboard OK':False,'Lift Problem':True})
    for side, open_input, closed_input in [('Starboard','X02','X01'), ('Port','X04','X03')]:
        values['binary_sensor'][f'Valve {side} FB — Fully Open ({open_input})'] = name in ('moving','emergency')
        values['binary_sensor'][f'Valve {side} FB — Fully Closed ({closed_input})'] = name not in ('moving','emergency')
    if name == 'unknown':
        values['sensor']['Lift Water Temperature'] = None
        for key in values['binary_sensor']:
            if key.startswith('Valve '): values['binary_sensor'][key] = None
    if name == 'valves':
        values['binary_sensor']['Valve Starboard FB — Fully Open (X02)'] = True
        values['binary_sensor']['Valve Port FB — Fully Closed (X03)'] = False
        values['text_sensor']['Valve Positions'] = 'Starboard FAULT · Port MOVING'
    with lock:
        for domain, group in values.items():
            for key, value in group.items():
                update(domain,key,value)


scene('normal')


class Handler(BaseHTTPRequestHandler):
    def send(self, code, content, kind='application/json'):
        if not isinstance(content, bytes):
            content = json.dumps(content).encode()
        self.send_response(code)
        self.send_header('Content-Type', kind)
        self.send_header('Cache-Control', 'no-store')
        self.send_header('Content-Length', str(len(content)))
        self.end_headers()
        self.wfile.write(content)

    def do_GET(self):
        global connected
        url = urlsplit(self.path)
        if url.path == '/':
            name = parse_qs(url.query).get('scene', ['normal'])[0]
            scene(name)
            self.send(200, b'<!doctype html><html><head><meta charset="utf-8"><link rel="stylesheet" href="/0.css"></head><body><div style="text-align:center;padding:8px;font:11px system-ui;color:#63766e">LOCAL PREVIEW &middot; Simulated readings &middot; No lift connected</div><esp-app></esp-app><script type="module" src="/0.js"></script></body></html>', 'text/html')
        elif url.path in ('/0.js','/0.css'):
            name = 'dashboard.js' if url.path == '/0.js' else 'dashboard.css'
            self.send(200,(ROOT / 'web' / name).read_bytes(),'text/javascript' if name.endswith('.js') else 'text/css')
        elif url.path == '/events':
            if not connected:
                self.send(503, {})
                return
            client = Queue()
            with lock:
                clients.append(client)
                initial = [dict(value) for value in entities.values()]
            self.send_response(200)
            self.send_header('Content-Type','text/event-stream')
            self.end_headers()
            try:
                for data in initial:
                    self.wfile.write(('event: state\ndata: '+json.dumps(data)+'\n\n').encode())
                self.wfile.flush()
                while connected:
                    try:
                        data = client.get(timeout=1)
                        packet = 'event: state\ndata: '+json.dumps(data)+'\n\n'
                    except Empty:
                        packet = 'event: ping\ndata: {}\n\n'
                    self.wfile.write(packet.encode()); self.wfile.flush()
            except (BrokenPipeError,ConnectionResetError,ConnectionAbortedError):
                pass
            finally:
                with lock:
                    clients.remove(client)
        elif url.path == '/test/requests':
            self.send(200, requests)
        elif url.path == '/test/drop':
            connected = False
            self.send(200, {'connected':False})
        else:
            parts = [unquote(p) for p in url.path.strip('/').split('/')]
            if len(parts) == 2 and tuple(parts) in entities:
                self.send(200, entities[tuple(parts)])
            else:
                self.send(404, {})

    def do_POST(self):
        url = urlsplit(self.path)
        parts = [unquote(p) for p in url.path.strip('/').split('/')]
        if len(parts) != 3 or tuple(parts[:2]) not in entities:
            self.send(404, {})
            return
        domain,name,action = parts
        values = parse_qs(url.query)
        requests.append({'domain':domain,'name':name,'action':action,'values':values})
        with lock:
            if domain == 'number' and action == 'set':
                update(domain,name,values['value'][0])
            if domain == 'switch':
                update(domain,name,action == 'turn_on')
            if domain == 'button' and name in ('Lift','Ready','Guide','Lower','Go to Height'):
                update('text_sensor','Lift Status',f"Raising -> {'Height' if name == 'Go to Height' else name}")
                update('text_sensor','Lift Activity','Raising')
            if domain == 'button' and name == 'Stop':
                update('text_sensor','Lift Status','Holding')
                update('text_sensor','Lift Activity','Idle')
                update('text_sensor','Last Stop Reason','Stop pressed')
                for key in ['Blower Relay (Y1) — bench only','Valve Starboard (Y2) — bench only','Valve Port (Y3) — bench only']:
                    update('switch',key,False)
        if slow_response and domain == 'number':
            time.sleep(3)
        try:
            self.send(200, {})
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass

    def log_message(self,*args):
        pass


if __name__ == '__main__':
    print('Local UI fixture: http://127.0.0.1:8767/ (no device connection)', flush=True)
    ThreadingHTTPServer(('127.0.0.1',8767),Handler).serve_forever()
