import AppKit
import FlasherCore
import SwiftUI
import UniformTypeIdentifiers

@MainActor
final class FlasherModel: ObservableObject {
    enum Phase: Equatable {
        case setup
        case preparing
        case flashing
        case finished
        case failed(String)
    }

    let targets: Targets

    @Published private(set) var keyboard: Keyboard
    /// Names of the devices to flash (all of them unless some are clicked off).
    @Published private(set) var selected: Set<String>
    @Published var reset = false
    /// A CI firmware .zip, or a folder of .uf2 files.
    @Published var firmware: URL?

    @Published private(set) var phase: Phase = .setup
    @Published private(set) var plan: [Step] = []
    @Published private(set) var current = 0
    @Published private(set) var status: StepStatus = .waiting

    private var roots = Drives.volumes
    private var task: Task<Void, Never>?
    private var startWhenShown = false

    init() {
        targets = Self.loadTargets()
        keyboard = targets.keyboards[0]
        selected = Set(targets.keyboards[0].devices.map(\.name))
        firmware = Firmware.newestDownload()
        readArguments()
    }

    /// targets.json is copied into the .app by build.sh; from `swift run` it is read from the repo.
    private static func loadTargets() -> Targets {
        let inRepo = URL(fileURLWithPath: #filePath)
            .deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
            .deletingLastPathComponent().appendingPathComponent("targets.json")
        let url = Bundle.main.url(forResource: "targets", withExtension: "json") ?? inRepo
        do {
            return try Targets.load(from: url)
        } catch {
            fatalError("Can't read \(url.path): \(error)")
        }
    }

    /// For smoke.sh: start already set up, against pretend drives.
    private func readArguments() {
        var arguments = CommandLine.arguments.dropFirst().makeIterator()
        while let argument = arguments.next() {
            switch argument {
            case "--keyboard":
                if let id = arguments.next(), let match = targets.keyboards.first(where: { $0.id == id }) {
                    choose(match)
                }
            case "--firmware":
                if let path = arguments.next() {
                    firmware = URL(fileURLWithPath: path)
                }
            case "--reset": reset = true
            case "--start": startWhenShown = true
            case "--volumes":
                if let path = arguments.next() { roots = [URL(fileURLWithPath: path)] }
            default: break
            }
        }
    }

    // MARK: Setting up

    func choose(_ keyboard: Keyboard) {
        self.keyboard = keyboard
        selected = Set(keyboard.devices.map(\.name))
    }

    func toggle(_ device: Device) {
        if selected.contains(device.name) {
            selected.remove(device.name)
        } else {
            selected.insert(device.name)
        }
    }

    var canStart: Bool { firmware != nil && !selected.isEmpty }

    func chooseFile() {
        let panel = NSOpenPanel()
        panel.message = "Choose the firmware .zip from a build, or a folder of .uf2 files"
        panel.allowedContentTypes = [.zip, .folder]
        panel.canChooseFiles = true
        panel.canChooseDirectories = true
        if panel.runModal() == .OK, let url = panel.url {
            firmware = url
        }
    }

    // MARK: Flashing

    var step: Step? { plan.indices.contains(current) ? plan[current] : nil }

    func shown() {
        if startWhenShown {
            startWhenShown = false
            start()
        }
    }

    func start() {
        guard let firmware, canStart else { return }
        let keyboard = keyboard, selected = selected, reset = reset
        plan = []
        current = 0
        phase = .preparing
        task = Task {
            do {
                // Unzipping blocks, so keep it off the main thread.
                let plan = try await Task.detached { () throws -> [Step] in
                    try Firmware.plan(
                        keyboard: keyboard, only: selected, reset: reset, files: Firmware.unpack(firmware))
                }.value
                guard !Task.isCancelled else { return }
                self.plan = plan
                await run(from: 0)
            } catch {
                fail(error)
            }
        }
    }

    /// After a failure: carry on from the step that failed.
    func retry() {
        guard step != nil else { return start() }
        task = Task { await run(from: current) }
    }

    func stop() {
        task?.cancel()
        task = nil
        plan = []
        current = 0
        phase = .setup
    }

    private func run(from first: Int) async {
        phase = .flashing
        do {
            for index in plan.indices.dropFirst(first) {
                current = index
                status = .waiting
                let finished = Set(plan[..<index].map(\.device.name))
                try await flash(
                    plan[index], roots: roots, boards: targets.boards, ports: USBSerialPorts(), finished: finished
                ) { [weak self] status in
                    await self?.update(status)
                }
            }
            phase = .finished
        } catch is CancellationError {
            // stop() has already gone back to the start
        } catch {
            fail(error)
        }
    }

    private func update(_ status: StepStatus) {
        self.status = status
    }

    private func fail(_ error: Error) {
        guard !Task.isCancelled else { return }
        phase = .failed(error.localizedDescription)
    }

    // MARK: For the views

    enum DeviceProgress {
        case off, waiting, next, done
    }

    func progress(of device: Device) -> DeviceProgress {
        let steps = plan.filter { $0.device == device }
        guard let last = steps.last else { return .off }
        if phase == .finished || last.id < current { return .done }
        return steps.contains { $0.id == current } ? .next : .waiting
    }

    func boardLabel(_ board: String) -> String { targets.boards[board]?.label ?? board }
}
