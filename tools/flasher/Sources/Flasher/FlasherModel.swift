import AppKit
import FlasherCore
import SwiftUI
import UniformTypeIdentifiers

@MainActor
final class FlasherModel: ObservableObject {
    enum Phase: Equatable {
        case setup
        case preparing(String)
        case flashing
        case finished
        case failed(String)
    }

    enum Source: Equatable {
        case file(URL)
        case latestBuild
    }

    let targets: Targets
    let canDownload = GitHub.gh() != nil

    @Published private(set) var keyboard: Keyboard
    /// Names of the devices to flash (all of them unless some are clicked off).
    @Published private(set) var selected: Set<String>
    @Published var reset = false
    @Published var source: Source?

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
        source = Firmware.newestDownload().map(Source.file)
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

    /// Lets it be opened ready to go, e.g.
    /// `open "ZMK Flasher.app" --args --keyboard waterfowl-dongle --reset --start`.
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
                    source = .file(URL(fileURLWithPath: (path as NSString).expandingTildeInPath))
                }
            case "--download": source = .latestBuild
            case "--reset": reset = true
            case "--start": startWhenShown = true
            case "--volumes":  // testing: look for bootloader drives here instead of /Volumes
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

    var canStart: Bool { source != nil && !selected.isEmpty }

    func chooseFile() {
        let panel = NSOpenPanel()
        panel.message = "Choose the firmware .zip from a build, or a folder of .uf2 files"
        panel.allowedContentTypes = [.zip, .folder]
        panel.canChooseFiles = true
        panel.canChooseDirectories = true
        if panel.runModal() == .OK, let url = panel.url {
            source = .file(url)
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
        guard let source, canStart else { return }
        let keyboard = keyboard, selected = selected, reset = reset, repo = targets.repo
        plan = []
        current = 0
        phase = .preparing(source == .latestBuild ? "Downloading the latest build…" : "Reading the firmware…")
        task = Task {
            do {
                // Unzipping and downloading block, so keep them off the main thread.
                let plan = try await Task.detached { () throws -> [Step] in
                    let folder: URL
                    switch source {
                    case .file(let url): folder = url
                    case .latestBuild: folder = try GitHub.downloadLatest(repo: repo)
                    }
                    return try Firmware.plan(
                        keyboard: keyboard, only: selected, reset: reset, files: Firmware.unpack(folder))
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
                try await flash(plan[index], roots: roots, boards: targets.boards) { [weak self] status in
                    await self?.update(status)
                }
                NSSound(named: "Glass")?.play()
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
        NSSound(named: "Basso")?.play()
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
