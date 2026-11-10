import Foundation

/// The USB serial ports that keyboards running this repo's firmware offer
/// (src/usb_flash_trigger.c): they say which shield they were built for, and
/// reboot into the bootloader when the port is set to 1200 baud.
public protocol SerialPorts: Sendable {
    /// Ports that might be a keyboard.
    func paths() -> [String]

    /// Opens a port and asks what it is. Returns nil if it isn't one of ours,
    /// otherwise its shield ("waterfowl_left", "settings_reset"...). If
    /// `enter` says yes to that, the device is sent into its bootloader.
    func probe(_ path: String, enter: (String) -> Bool) -> String?
}

public struct USBSerialPorts: SerialPorts {
    static let greeting = "zmk-flasher"

    public init() {}

    public func paths() -> [String] {
        ((try? FileManager.default.contentsOfDirectory(atPath: "/dev")) ?? [])
            .filter { $0.hasPrefix("cu.usbmodem") }
            .map { "/dev/" + $0 }
            .sorted()
    }

    public func probe(_ path: String, enter: (String) -> Bool) -> String? {
        let port = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK)
        guard port >= 0 else { return nil }
        defer { close(port) }

        var settings = termios()
        guard tcgetattr(port, &settings) == 0 else { return nil }
        cfmakeraw(&settings)
        cfsetspeed(&settings, speed_t(B115200))
        tcsetattr(port, TCSANOW, &settings)

        guard let name = Self.name(in: readLine(port, seconds: 1.5)) else { return nil }
        if enter(name) {
            cfsetspeed(&settings, speed_t(B1200))
            tcsetattr(port, TCSANOW, &settings)
            usleep(300_000)  // long enough for the firmware to notice
        }
        return name
    }

    /// "zmk-flasher waterfowl_left" -> "waterfowl_left"; nil for anything else.
    static func name(in line: String) -> String? {
        let words = line.split(separator: " ", omittingEmptySubsequences: true)
        guard words.first.map(String.init) == greeting, words.count <= 2 else { return nil }
        return words.count == 2 ? String(words[1]) : ""
    }

    private func readLine(_ port: Int32, seconds: TimeInterval) -> String {
        var bytes: [UInt8] = []
        let deadline = Date().addingTimeInterval(seconds)
        while Date() < deadline, !bytes.contains(UInt8(ascii: "\n")), bytes.count < 256 {
            var waiting = pollfd(fd: port, events: Int16(POLLIN), revents: 0)
            guard poll(&waiting, 1, 100) > 0 else { continue }
            var chunk = [UInt8](repeating: 0, count: 64)
            let count = read(port, &chunk, chunk.count)
            if count > 0 { bytes += chunk[..<count] }
        }
        let line = bytes.prefix { $0 != UInt8(ascii: "\n") }
        return String(decoding: line, as: UTF8.self).trimmingCharacters(in: .whitespacesAndNewlines)
    }
}
