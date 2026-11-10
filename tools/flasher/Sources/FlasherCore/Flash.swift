import Foundation

public enum Drives {
    public static let volumes = [URL(fileURLWithPath: "/Volumes")]

    /// Mounted drives that look like a UF2 bootloader (they have INFO_UF2.TXT).
    public static func bootloaders(in roots: [URL] = volumes) -> [URL] {
        roots
            .flatMap { (try? FileManager.default.contentsOfDirectory(at: $0, includingPropertiesForKeys: nil)) ?? [] }
            .filter { FileManager.default.fileExists(atPath: $0.appendingPathComponent("INFO_UF2.TXT").path) }
            .sorted { $0.path < $1.path }
    }

    /// Which kind of board a bootloader drive says it is: nil if it doesn't
    /// say, and nil for the whole thing if the drive can't be read (it has gone).
    public static func board(of drive: URL, boards: [String: Board]) -> String?? {
        guard let data = try? Data(contentsOf: drive.appendingPathComponent("INFO_UF2.TXT")) else { return nil }
        let info = String(decoding: data, as: UTF8.self).lowercased()
        return .some(boards.keys.sorted().first { key in boards[key]!.hints.contains { info.contains($0) } })
    }
}

/// Where one flash has got to.
public enum StepStatus: Equatable, Sendable {
    /// Nothing to flash yet: the person needs to plug the device in (and
    /// double-tap reset, if its firmware is too old to be asked over USB).
    case waiting
    /// A different device of this keyboard is plugged in ("left", "right", "dongle").
    case wrongDevice(String)
    /// The device was found over USB and told to start its bootloader.
    case starting
    /// A drive appeared but it is the other kind of board. Nothing is copied.
    case wrongBoard(String)
    /// More than one bootloader drive is mounted, so it isn't clear which to use.
    case tooManyDrives
    case copying
    /// Copied; waiting for the drive to go away, which is the board restarting.
    case restarting
    case done
}

public struct FlashTiming: Sendable {
    public var poll: TimeInterval = 0.5
    /// Pause between seeing the drive and copying, to let macOS finish mounting it.
    public var settle: TimeInterval = 1
    public var restart: TimeInterval = 30

    public init() {}
    public init(poll: TimeInterval, settle: TimeInterval, restart: TimeInterval) {
        self.poll = poll
        self.settle = settle
        self.restart = restart
    }
}

/// Waits for the step's device to show up as a bootloader drive, copies the
/// file and waits for the drive to disappear again. Waits for as long as it
/// takes; cancel the task to stop.
///
/// A device that is plugged in and running firmware with the USB trigger is
/// sent into its bootloader, unless it says it is a different device.
/// `finished` names devices already flashed in this run: those are expected
/// to still be plugged in, so they aren't reported as the wrong device.
public func flash(
    _ step: Step,
    roots: [URL] = Drives.volumes,
    boards: [String: Board],
    ports: SerialPorts? = nil,
    finished: Set<String> = [],
    timing: FlashTiming = FlashTiming(),
    report: @escaping @Sendable (StepStatus) async -> Void
) async throws {
    var last: StepStatus?
    func say(_ status: StepStatus) async {
        if status != last {
            last = status
            await report(status)
        }
    }
    func pause(_ seconds: TimeInterval) async throws {
        try await Task.sleep(nanoseconds: UInt64(seconds * 1_000_000_000))
    }

    var asked: [String: String?] = [:]  // port -> what it said it was (nil: not a keyboard)
    var startingUntil = Date.distantPast

    var drive: URL?
    while drive == nil {
        try Task.checkCancellation()
        let found = Drives.bootloaders(in: roots)
        if found.count > 1 {
            await say(.tooManyDrives)
        } else if let only = found.first, let board = Drives.board(of: only, boards: boards) {
            if let board, board != step.device.board {
                await say(.wrongBoard(board))
            } else {
                drive = only
                break
            }
        } else {
            if let ports {
                let present = ports.paths()
                asked = asked.filter { present.contains($0.key) }
                for path in present where asked[path] == nil {
                    let mine: (String) -> Bool = { $0.isEmpty || $0 == step.device.name }
                    let name = ports.probe(path, enter: mine)
                    asked[path] = .some(name)
                    if let name, mine(name) { startingUntil = Date().addingTimeInterval(10) }
                }
            }
            let others = asked.values.compactMap { $0 }
                .filter { !$0.isEmpty && $0 != step.device.name && !finished.contains($0) }
            if Date() < startingUntil {
                await say(.starting)
            } else if let other = others.sorted().first {
                await say(.wrongDevice(other))
            } else {
                await say(.waiting)
            }
        }
        try await pause(timing.poll)
    }
    guard let drive else { return }

    await say(.copying)
    try await pause(timing.settle)
    let firmware = try Data(contentsOf: step.uf2)
    do {
        try firmware.write(to: drive.appendingPathComponent(step.uf2.lastPathComponent))
    } catch {
        // The board restarts as soon as it has the whole file, which can make
        // the copy itself report an error. Judge by whether the drive goes away.
    }

    await say(.restarting)
    let deadline = Date().addingTimeInterval(timing.restart)
    while FileManager.default.fileExists(atPath: drive.path) {
        if Date() > deadline { throw FlashError.stillMounted(drive.lastPathComponent) }
        try await pause(timing.poll)
    }
    await say(.done)
}
