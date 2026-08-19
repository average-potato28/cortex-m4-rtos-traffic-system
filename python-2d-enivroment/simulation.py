import os
import threading
import pygame
import sys
import pygame_gui
import serial
import time
import random

PORT = os.environ.get('STM32_PORT', 'COM3')
BAUD = 9600

stm32 = None
last_heartbeat = None
stm32_connected = False
uart_lock = threading.Lock()

dir_chars_signal = {0: 'r', 1: 'd', 2: 'l', 3: 'u'}
dir_chars_queue = {0: 'R', 1: 'D', 2: 'L', 3: 'U'}
directionNumbers = {0: 'right', 1: 'down', 2: 'left', 3: 'up'}

signal_state = (-1, 0)
noOfSignals = 4

global_spawn_delay = 1.0
last_emergency_spawn_time = 0.0
speeds = {'car': 2.25, 'bus': 1.8, 'truck': 1.8, 'bike': 2.5, 'emergency': 3.5}

default_x = {'right': [0, 0, 0], 'down': [755, 727, 697], 'left': [1400, 1400, 1400], 'up': [602, 627, 657]}
default_y = {'right': [348, 370, 398], 'down': [0, 0, 0], 'left': [498, 466, 436], 'up': [800, 800, 800]}

vehicles = {
    'right': {0: [], 1: [], 2: [], 'crossed': 0},
    'down':  {0: [], 1: [], 2: [], 'crossed': 0},
    'left':  {0: [], 1: [], 2: [], 'crossed': 0},
    'up':    {0: [], 1: [], 2: [], 'crossed': 0}
}
vehicleTypes = {0: 'car', 1: 'bus', 2: 'truck', 3: 'bike', 4: 'emergency'}

signalCoods = [(530, 230), (810, 230), (810, 570), (530, 570)]
signalTimerCoods = [(530, 210), (810, 210), (810, 550), (530, 550)]

stopLines = {'right': 590, 'down': 330, 'left': 800, 'up': 535}
defaultStop = {'right': 580, 'down': 320, 'left': 810, 'up': 545}

stoppingGap = 15
movingGap = 15

pygame.init()
simulation = pygame.sprite.Group()

class Vehicle(pygame.sprite.Sprite):
    def __init__(self, lane, vehicleClass, direction_number, direction):
        super().__init__()
        self.lane = lane
        self.vehicleClass = vehicleClass
        self.speed = speeds[vehicleClass]
        self.direction_number = direction_number
        self.direction = direction
        self.crossed = 0

        path = f"images/{direction}/{vehicleClass}.png"
        try:
            self.image = pygame.image.load(path)
        except Exception:
            self.image = pygame.Surface((30, 20))
            self.image.fill((255, 0, 0) if vehicleClass == 'emergency' else (0, 0, 255))

        spawn_x = default_x[direction][lane]
        spawn_y = default_y[direction][lane]

        if len(vehicles[direction][lane]) > 0:
            last_veh = vehicles[direction][lane][-1]
            if direction == 'right':
                spawn_x = min(spawn_x, last_veh.x - self.image.get_rect().width - stoppingGap)
            elif direction == 'left':
                spawn_x = max(spawn_x, last_veh.x + last_veh.image.get_rect().width + stoppingGap)
            elif direction == 'down':
                spawn_y = min(spawn_y, last_veh.y - self.image.get_rect().height - stoppingGap)
            elif direction == 'up':
                spawn_y = max(spawn_y, last_veh.y + last_veh.image.get_rect().height + stoppingGap)

        self.x = spawn_x
        self.y = spawn_y

        vehicles[direction][lane].append(self)
        self.index = len(vehicles[direction][lane]) - 1

        self.update_stop()
        simulation.add(self)

    def update_stop(self):
        if self.index == 0:
            self.stop = defaultStop[self.direction]
        else:
            front_veh = vehicles[self.direction][self.lane][self.index - 1]
            if front_veh.crossed == 0:
                if self.direction == 'right': self.stop = front_veh.stop - front_veh.image.get_rect().width - stoppingGap
                elif self.direction == 'left': self.stop = front_veh.stop + front_veh.image.get_rect().width + stoppingGap
                elif self.direction == 'down': self.stop = front_veh.stop - front_veh.image.get_rect().height - stoppingGap
                elif self.direction == 'up': self.stop = front_veh.stop + front_veh.image.get_rect().height + stoppingGap
            else:
                self.stop = defaultStop[self.direction]

    def can_change_lane(self, target_lane):
        for v in vehicles[self.direction][target_lane]:
            if self.direction in ['right', 'left']:
                if abs(self.x - v.x) < 80: return False
            else:
                if abs(self.y - v.y) < 80: return False
        return True

    def change_lane(self, target_lane):
        if not self.can_change_lane(target_lane): return

        old_list = vehicles[self.direction][self.lane]
        if self in old_list:
            old_list.remove(self)
            for i, v in enumerate(old_list): v.index = i

        new_list = vehicles[self.direction][target_lane]
        self.lane = target_lane

        insert_idx = len(new_list)
        for i, v in enumerate(new_list):
            if self.direction == 'right' and self.x > v.x: insert_idx = i; break
            elif self.direction == 'left' and self.x < v.x: insert_idx = i; break
            elif self.direction == 'down' and self.y > v.y: insert_idx = i; break
            elif self.direction == 'up' and self.y < v.y: insert_idx = i; break

        new_list.insert(insert_idx, self)
        for i, v in enumerate(new_list): v.index = i

    def move(self):
        self.update_stop()

        if self.crossed == 0 and self.vehicleClass != 'emergency':
            ev_approaching = False
            for check_lane in range(self.lane, 3):
                for v in vehicles[self.direction][check_lane]:
                    if v.vehicleClass == 'emergency':
                        dist = 0
                        is_behind = False
                        if self.direction == 'right' and v.x <= self.x: dist = self.x - v.x; is_behind = True
                        elif self.direction == 'left' and v.x >= self.x: dist = v.x - self.x; is_behind = True
                        elif self.direction == 'down' and v.y <= self.y: dist = self.y - v.y; is_behind = True
                        elif self.direction == 'up' and v.y >= self.y: dist = v.y - self.y; is_behind = True

                        if is_behind and dist < 150:
                            ev_approaching = True
                            break
                if ev_approaching: break

            if ev_approaching:
                if self.lane > 0: self.change_lane(self.lane - 1)
            else:
                if self.lane == 0 and self.vehicleClass != 'bike': self.change_lane(1)

        if self.direction in ['right', 'left']:
            target_y = default_y[self.direction][self.lane]
            if self.y < target_y: self.y = min(self.y + 2, target_y)
            elif self.y > target_y: self.y = max(self.y - 2, target_y)
        elif self.direction in ['up', 'down']:
            target_x = default_x[self.direction][self.lane]
            if self.x < target_x: self.x = min(self.x + 2, target_x)
            elif self.x > target_x: self.x = max(self.x - 2, target_x)

        current_green_dir, current_yellow_flag = signal_state
        is_green = (current_green_dir == self.direction_number and current_yellow_flag == 0)

        if self.direction == 'right':
            if self.crossed == 0 and self.x + self.image.get_rect().width > stopLines[self.direction]:
                self.crossed = 1
                if self.vehicleClass == 'emergency':
                    notify_emergency_cleared(self.direction_number)
            if (self.x + self.image.get_rect().width <= self.stop or self.crossed == 1 or is_green) and \
               (self.index == 0 or self.x + self.image.get_rect().width < (vehicles[self.direction][self.lane][self.index - 1].x - movingGap)):
                self.x += self.speed
        elif self.direction == 'down':
            if self.crossed == 0 and self.y + self.image.get_rect().height > stopLines[self.direction]:
                self.crossed = 1
                if self.vehicleClass == 'emergency':
                    notify_emergency_cleared(self.direction_number)
            if (self.y + self.image.get_rect().height <= self.stop or self.crossed == 1 or is_green) and \
               (self.index == 0 or self.y + self.image.get_rect().height < (vehicles[self.direction][self.lane][self.index - 1].y - movingGap)):
                self.y += self.speed
        elif self.direction == 'left':
            if self.crossed == 0 and self.x < stopLines[self.direction]:
                self.crossed = 1
                if self.vehicleClass == 'emergency':
                    notify_emergency_cleared(self.direction_number)
            if (self.x >= self.stop or self.crossed == 1 or is_green) and \
               (self.index == 0 or self.x > (vehicles[self.direction][self.lane][self.index - 1].x + vehicles[self.direction][self.lane][self.index - 1].image.get_rect().width + movingGap)):
                self.x -= self.speed
        elif self.direction == 'up':
            if self.crossed == 0 and self.y < stopLines[self.direction]:
                self.crossed = 1
                if self.vehicleClass == 'emergency':
                    notify_emergency_cleared(self.direction_number)
            if (self.y >= self.stop or self.crossed == 1 or is_green) and \
               (self.index == 0 or self.y > (vehicles[self.direction][self.lane][self.index - 1].y + vehicles[self.direction][self.lane][self.index - 1].image.get_rect().height + movingGap)):
                self.y -= self.speed

        if self.crossed == 1:
            if self.x > 1500 or self.x < -100 or self.y > 900 or self.y < -100:
                self.kill()
                if self in vehicles[self.direction][self.lane]:
                    vehicles[self.direction][self.lane].remove(self)
                    for i, v in enumerate(vehicles[self.direction][self.lane]):
                        v.index = i

def spawn_single_vehicle():
    lane_number = random.randint(0, 2)
    vehicle_type = 3 if lane_number == 0 else random.choice([0, 1, 2])

    temp = random.randint(0, 99)
    if temp < 25: direction_number = 0
    elif temp < 50: direction_number = 1
    elif temp < 75: direction_number = 2
    else: direction_number = 3

    Vehicle(lane_number, vehicleTypes[vehicle_type], direction_number, directionNumbers[direction_number])

def spawn_emergency():
    global last_emergency_spawn_time
    now = time.time()
    if now - last_emergency_spawn_time < 2.0:
        return
    last_emergency_spawn_time = now

    direction_number = random.randint(0, 3)
    lane_number = 2
    direction = directionNumbers[direction_number]
    Vehicle(lane_number, 'emergency', direction_number, direction)

    with uart_lock:
        if stm32 is not None and stm32_connected:
            try:
                msg = f"E{dir_chars_signal[direction_number]}\n"
                stm32.write(msg.encode())
                stm32.flush()
                print(f"UART TX -> Sent Emergency Override: {msg.strip()}")
            except Exception as ex:
                print(f"UART TX Error: {ex}")

def notify_emergency_cleared(direction_number):
    with uart_lock:
        if stm32 is not None and stm32_connected:
            try:
                msg = f"C{dir_chars_signal[direction_number]}\n"
                stm32.write(msg.encode())
                stm32.flush()
                print(f"UART TX -> Sent Emergency Cleared: {msg.strip()}")
            except Exception as ex:
                print(f"UART TX Error: {ex}")

def serial_communication_thread():
    global stm32, stm32_connected, last_heartbeat, signal_state
    char_to_dir = {'r': 0, 'd': 1, 'l': 2, 'u': 3}

    while True:
        if stm32 is None:
            try:
                new_conn = serial.Serial(PORT, BAUD, timeout=0.1)
                with uart_lock:
                    stm32 = new_conn
                print(f"STM32 Serial Port Connected on {PORT}.")
            except Exception:
                time.sleep(1)
                continue

        try:
            while stm32.in_waiting > 0:
                msg = stm32.readline().decode('utf-8', errors='ignore').strip()
                if not msg:
                    continue

                if msg == 'i1':
                    last_heartbeat = time.time()
                    stm32_connected = True
                elif len(msg) >= 3 and msg[0] in char_to_dir:
                    last_heartbeat = time.time()
                    stm32_connected = True

                    dir_idx = char_to_dir[msg[0]]
                    state = msg[1:3]

                    if state == '11':
                        signal_state = (dir_idx, 0)
                    elif state == '01':
                        signal_state = (dir_idx, 1)
                    elif state == '00':
                        curr_dir, curr_yellow = signal_state
                        if curr_dir == dir_idx:
                            signal_state = (-1, 0)
        except (serial.SerialException, OSError) as e:
            print(f"STM32 Disconnected: {e}")
            with uart_lock:
                if stm32 is not None:
                    try:
                        stm32.close()
                    except Exception:
                        pass
                    stm32 = None
                stm32_connected = False
                signal_state = (-1, 0)

        if stm32_connected and last_heartbeat is not None and (time.time() - last_heartbeat > 3.0):
            print("STM32 Heartbeat Lost! All signals halted on RED.")
            stm32_connected = False
            signal_state = (-1, 0)

        time.sleep(0.01)

def transmit_queue_lengths():
    while True:
        for dir_num, dir_name in directionNumbers.items():
            total_waiting_vehicles = 0
            for lane in range(3):
                for vehicle in vehicles[dir_name][lane]:
                    if vehicle.crossed == 0:
                        if dir_name == 'right':
                            dist = stopLines[dir_name] - (vehicle.x + vehicle.image.get_rect().width)
                        elif dir_name == 'left':
                            dist = vehicle.x - stopLines[dir_name]
                        elif dir_name == 'down':
                            dist = stopLines[dir_name] - (vehicle.y + vehicle.image.get_rect().height)
                        elif dir_name == 'up':
                            dist = vehicle.y - stopLines[dir_name]

                        if 0 <= dist <= 300:
                            total_waiting_vehicles += 1

            queue_meters = min(50, total_waiting_vehicles * 6)
            queue_str = f"{dir_chars_queue[dir_num]}{queue_meters:02d}\n"
            
            with uart_lock:
                if stm32 is not None and stm32_connected:
                    try:
                        stm32.write(queue_str.encode())
                    except Exception:
                        pass
        time.sleep(0.3)

def main():
    global global_spawn_delay

    thread_serial = threading.Thread(name="serial_comm", target=serial_communication_thread, daemon=True)
    thread_serial.start()

    thread_queue = threading.Thread(name="queue_tx", target=transmit_queue_lengths, daemon=True)
    thread_queue.start()

    white = (255, 255, 255)
    screenWidth = 1400
    screenHeight = 800
    screenSize = (screenWidth, screenHeight)

    try:
        background = pygame.image.load('images/intersection.png')
    except Exception:
        background = pygame.Surface(screenSize)
        background.fill((60, 60, 60))

    screen = pygame.display.set_mode(screenSize)
    pygame.display.set_caption("STM32 HARDWARE-IN-THE-LOOP TRAFFIC CONTROLLER")

    try:
        redSignal = pygame.image.load('images/signals/red.png')
        yellowSignal = pygame.image.load('images/signals/yellow.png')
        greenSignal = pygame.image.load('images/signals/green.png')
    except Exception:
        redSignal = pygame.Surface((30, 30)); redSignal.fill((255, 0, 0))
        yellowSignal = pygame.Surface((30, 30)); yellowSignal.fill((255, 255, 0))
        greenSignal = pygame.Surface((30, 30)); greenSignal.fill((0, 255, 0))

    status_font = pygame.font.Font(None, 24)
    clock = pygame.time.Clock()
    manager = pygame_gui.UIManager(screenSize)

    emergency_btn = pygame_gui.elements.UIButton(
        relative_rect=pygame.Rect((50, 50), (160, 40)),
        text='Spawn Emergency',
        manager=manager
    )

    spawn_slider = pygame_gui.elements.UIHorizontalSlider(
        relative_rect=pygame.Rect((50, 110), (200, 20)),
        start_value=1.0,
        value_range=(0.1, 3.0),
        manager=manager
    )

    spawn_accumulator = 0.0

    while True:
        time_delta = clock.tick(60) / 1000.0

        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                sys.exit()

            manager.process_events(event)

            if event.type == pygame_gui.UI_BUTTON_PRESSED:
                if event.ui_element == emergency_btn:
                    spawn_emergency()

            if event.type == pygame_gui.UI_HORIZONTAL_SLIDER_MOVED:
                if event.ui_element == spawn_slider:
                    global global_spawn_delay
                    global_spawn_delay = event.value

        spawn_accumulator += time_delta
        if spawn_accumulator >= global_spawn_delay:
            spawn_single_vehicle()
            spawn_accumulator = 0.0

        screen.blit(background, (0, 0))

        current_green_dir, current_yellow_flag = signal_state

        for i in range(noOfSignals):
            if i == current_green_dir:
                if current_yellow_flag == 1:
                    screen.blit(yellowSignal, signalCoods[i])
                else:
                    screen.blit(greenSignal, signalCoods[i])
            else:
                screen.blit(redSignal, signalCoods[i])

        for vehicle in simulation:
            screen.blit(vehicle.image, [vehicle.x, vehicle.y])
            vehicle.move()

        manager.update(time_delta)
        manager.draw_ui(screen)

        badge_width, badge_height = 180, 36
        badge_x = (screenWidth - badge_width) // 2
        badge_y = 15

        badge_bg = (34, 139, 34) if stm32_connected else (178, 34, 34)
        text_str = "STM32 ONLINE" if stm32_connected else "STM32 OFFLINE"

        pygame.draw.rect(screen, badge_bg, (badge_x, badge_y, badge_width, badge_height), border_radius=8)
        pygame.draw.rect(screen, white, (badge_x, badge_y, badge_width, badge_height), width=2, border_radius=8)

        status_surface = status_font.render(text_str, True, white)
        text_rect = status_surface.get_rect(center=(screenWidth // 2, badge_y + badge_height // 2))
        screen.blit(status_surface, text_rect)

        pygame.display.update()

if __name__ == '__main__':
    main()
