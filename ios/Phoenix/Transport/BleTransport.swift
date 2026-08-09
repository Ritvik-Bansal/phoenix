import Foundation
import Combine
import CoreBluetooth

/// Real hardware transport. CoreBluetooth is non-functional in the iOS
/// Simulator, so this class only does anything on a device — everything above
/// it targets PhoenixTransport and runs on FakeTransport during development.
///
/// Implements: scan by service UUID, connect, discover, subscribe RX, chunked
/// TX writes at the negotiated MTU, exponential-backoff reconnection with
/// jitter, and State Preservation & Restoration so iOS can relaunch the app
/// into a live connection.
final class BleTransport: NSObject, PhoenixTransport {
    /// Restoration identifier — must be stable across launches for iOS to
    /// hand the connection back in willRestoreState.
    static let restoreIdentifier = "com.phoenix.central.restore"

    private let stateSubject = CurrentValueSubject<TransportState, Never>(.idle)
    private let framesSubject = PassthroughSubject<PhoenixFrame, Never>()
    private let logSubject = PassthroughSubject<FrameLog, Never>()

    var statePublisher: AnyPublisher<TransportState, Never> { stateSubject.eraseToAnyPublisher() }
    var incomingFrames: AnyPublisher<PhoenixFrame, Never> { framesSubject.eraseToAnyPublisher() }
    var frameLog: AnyPublisher<FrameLog, Never> { logSubject.eraseToAnyPublisher() }
    var state: TransportState { stateSubject.value }

    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var txChar: CBCharacteristic?
    private var rxChar: CBCharacteristic?

    private let decoder = PhoenixStreamDecoder()
    private var seqOut: UInt8 = 0
    private var backoff = ReconnectionBackoff()
    private var wantConnection = false
    private var reconnectWork: DispatchWorkItem?

    private let serviceUUID = CBUUID(string: PhoenixProto.serviceUUID)
    private let txUUID = CBUUID(string: PhoenixProto.txUUID)
    private let rxUUID = CBUUID(string: PhoenixProto.rxUUID)

    override init() {
        super.init()
        central = CBCentralManager(
            delegate: self,
            queue: nil,
            options: [
                CBCentralManagerOptionRestoreIdentifierKey: Self.restoreIdentifier,
                CBCentralManagerOptionShowPowerAlertKey: true,
            ])
    }

    func start() {
        wantConnection = true
        beginScanIfPossible()
    }

    func stop() {
        wantConnection = false
        reconnectWork?.cancel()
        central.stopScan()
        if let p = peripheral { central.cancelPeripheralConnection(p) }
        stateSubject.send(.idle)
    }

    func send(_ frame: PhoenixFrame) {
        var f = frame
        f.seq = seqOut
        seqOut &+= 1
        guard let bytes = f.encoded() else { return }
        logSubject.send(FrameLog(date: Date(), direction: .tx, bytes: bytes,
                                 summary: PhoenixMessages.describe(f)))
        guard let peripheral, let txChar else { return }

        // iOS decides the MTU; we can only ask what fits (PROTOCOL.md §1).
        let maxLen = peripheral.maximumWriteValueLength(for: .withoutResponse)
        let chunk = max(1, maxLen)
        var i = 0
        while i < bytes.count {
            let n = min(chunk, bytes.count - i)
            peripheral.writeValue(Data(bytes[i..<(i + n)]), for: txChar, type: .withoutResponse)
            i += n
        }
    }

    private func beginScanIfPossible() {
        guard wantConnection, central.state == .poweredOn else { return }
        // Prefer an already-connected system peripheral (e.g. after
        // restoration or a background relaunch) over a fresh scan.
        let known = central.retrieveConnectedPeripherals(withServices: [serviceUUID])
        if let p = known.first {
            connect(p)
            return
        }
        stateSubject.send(.scanning)
        central.scanForPeripherals(withServices: [serviceUUID], options: nil)
    }

    private func connect(_ p: CBPeripheral) {
        peripheral = p
        p.delegate = self
        central.stopScan()
        stateSubject.send(.connecting)
        central.connect(p, options: nil)
    }

    private func scheduleReconnect() {
        guard wantConnection else { return }
        reconnectWork?.cancel()
        let delay = backoff.nextDelay()
        let work = DispatchWorkItem { [weak self] in self?.beginScanIfPossible() }
        reconnectWork = work
        DispatchQueue.main.asyncAfter(deadline: .now() + delay, execute: work)
    }
}

extension BleTransport: CBCentralManagerDelegate {
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        switch central.state {
        case .poweredOn:
            beginScanIfPossible()
        case .poweredOff:
            stateSubject.send(.disconnected(reason: "Bluetooth off"))
        case .unauthorized:
            stateSubject.send(.disconnected(reason: "Bluetooth permission denied"))
        case .unsupported:
            stateSubject.send(.disconnected(reason: "Bluetooth unavailable (Simulator?)"))
        default:
            break
        }
    }

    /// State Preservation & Restoration: iOS relaunched us with live BLE state.
    func centralManager(_ central: CBCentralManager,
                        willRestoreState dict: [String: Any]) {
        if let peripherals = dict[CBCentralManagerRestoredStatePeripheralsKey] as? [CBPeripheral],
           let p = peripherals.first {
            wantConnection = true
            peripheral = p
            p.delegate = self
            // Already connected? Re-discover to repopulate characteristics.
            if p.state == .connected {
                stateSubject.send(.connecting)
                p.discoverServices([serviceUUID])
            } else {
                connect(p)
            }
        }
        if let scanned = dict[CBCentralManagerRestoredStateScanServicesKey] as? [CBUUID],
           scanned.contains(serviceUUID) {
            wantConnection = true
            stateSubject.send(.scanning)
        }
    }

    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral,
                        advertisementData: [String: Any], rssi RSSI: NSNumber) {
        connect(peripheral)
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        peripheral.discoverServices([serviceUUID])
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral,
                        error: Error?) {
        stateSubject.send(.disconnected(reason: error?.localizedDescription ?? "connect failed"))
        scheduleReconnect()
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral,
                        error: Error?) {
        txChar = nil
        rxChar = nil
        stateSubject.send(.disconnected(reason: error?.localizedDescription))
        scheduleReconnect()
    }
}

extension BleTransport: CBPeripheralDelegate {
    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        guard error == nil, let service = peripheral.services?.first(where: { $0.uuid == serviceUUID })
        else {
            stateSubject.send(.disconnected(reason: "service not found"))
            return
        }
        peripheral.discoverCharacteristics([txUUID, rxUUID], for: service)
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService,
                    error: Error?) {
        guard error == nil else { return }
        for c in service.characteristics ?? [] {
            if c.uuid == txUUID { txChar = c }
            if c.uuid == rxUUID {
                rxChar = c
                peripheral.setNotifyValue(true, for: c)
            }
        }
        if txChar != nil && rxChar != nil {
            backoff.reset()
            stateSubject.send(.connected)
            send(PhoenixMessages.ping())  // version handshake (PROTOCOL.md §5)
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic,
                    error: Error?) {
        guard error == nil, characteristic.uuid == rxUUID, let data = characteristic.value else { return }
        decoder.feed(data)
        while let frame = decoder.poll() {
            logSubject.send(FrameLog(date: Date(), direction: .rx,
                                     bytes: frame.encoded() ?? [],
                                     summary: PhoenixMessages.describe(frame)))
            framesSubject.send(frame)
        }
    }
}
