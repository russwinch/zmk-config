import Foundation
import XCTest

@testable import FlasherCore

final class FlasherCoreTests: XCTestCase {
    static let tools = URL(fileURLWithPath: #filePath)
        .deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()

    let targets = try! Targets.load(from: tools.appendingPathComponent("targets.json"))
    let fast = FlashTiming(poll: 0.02, settle: 0, restart: 1)
    var scratch: URL!

    override func setUpWithError() throws {
        scratch = FileManager.default.temporaryDirectory.appendingPathComponent("flasher-tests-" + UUID().uuidString)
        try FileManager.default.createDirectory(at: scratch, withIntermediateDirectories: true)
    }

    override func tearDownWithError() throws {
        try? FileManager.default.removeItem(at: scratch)
    }

    func keyboard(_ id: String) -> Keyboard { targets.keyboards.first { $0.id == id }! }

    /// A folder of files named the way CI names them.
    func firmwareFolder() throws -> [URL] {
        let names = [
            "waterfowl_dongle prospector_adapter-xiao_ble__zmk-zmk.uf2",
            "waterfowl_left nice_view_adapter nice_view-nice_nano__zmk-zmk.uf2",
            "waterfowl_left_peripheral_for_dongle.uf2",
            "waterfowl_right-nice_nano__zmk-zmk.uf2",
            "settings_reset-nice_nano__zmk-zmk.uf2",
            "settings_reset-xiao_ble__zmk-zmk.uf2",
        ]
        let folder = scratch.appendingPathComponent("firmware")
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        for name in names {
            try Data(name.utf8).write(to: folder.appendingPathComponent(name))
        }
        return try Firmware.unpack(folder)
    }

    /// Plays a board: shows up as a bootloader drive, and goes away once a .uf2 lands on it.
    func board(_ info: String, in volumes: URL, named name: String = "BOOT") -> Task<String?, Error> {
        Task {
            let drive = volumes.appendingPathComponent(name)
            try FileManager.default.createDirectory(at: drive, withIntermediateDirectories: true)
            try Data(info.utf8).write(to: drive.appendingPathComponent("INFO_UF2.TXT"))
            for _ in 0..<500 {
                let files = try FileManager.default.contentsOfDirectory(atPath: drive.path)
                if let uf2 = files.first(where: { $0.hasSuffix(".uf2") }) {
                    try FileManager.default.removeItem(at: drive)
                    return uf2
                }
                try await Task.sleep(nanoseconds: 10_000_000)
            }
            return nil
        }
    }

    func testFileNames() {
        let built = Firmware.key(for: URL(fileURLWithPath: "/x/waterfowl_left nice_view_adapter nice_view-nice_nano__zmk-zmk.uf2"))
        XCTAssertEqual(built.firmware, "waterfowl_left")
        XCTAssertEqual(built.board, "nice_nano")
        let named = Firmware.key(for: URL(fileURLWithPath: "/x/waterfowl_left_peripheral_for_dongle.uf2"))
        XCTAssertEqual(named.firmware, "waterfowl_left_peripheral_for_dongle")
        XCTAssertNil(named.board)
    }

    func testPlanWithResetIsInOrder() throws {
        let plan = try Firmware.plan(keyboard: keyboard("waterfowl-dongle"), reset: true, files: firmwareFolder())
        XCTAssertEqual(plan.map(\.device.name), ["dongle", "dongle", "left", "left", "right", "right"])
        XCTAssertEqual(plan.map(\.kind), [.settingsReset, .firmware, .settingsReset, .firmware, .settingsReset, .firmware])
        XCTAssertEqual(plan.map(\.uf2.lastPathComponent), [
            "settings_reset-xiao_ble__zmk-zmk.uf2",
            "waterfowl_dongle prospector_adapter-xiao_ble__zmk-zmk.uf2",
            "settings_reset-nice_nano__zmk-zmk.uf2",
            "waterfowl_left_peripheral_for_dongle.uf2",
            "settings_reset-nice_nano__zmk-zmk.uf2",
            "waterfowl_right-nice_nano__zmk-zmk.uf2",
        ])
    }

    func testPlanForSomeDevices() throws {
        let plan = try Firmware.plan(keyboard: keyboard("waterfowl"), only: ["right"], reset: false, files: firmwareFolder())
        XCTAssertEqual(plan.map(\.uf2.lastPathComponent), ["waterfowl_right-nice_nano__zmk-zmk.uf2"])
        XCTAssertThrowsError(try Firmware.plan(keyboard: keyboard("waterfowl"), only: [], reset: false, files: firmwareFolder()))
    }

    func testMissingFirmware() throws {
        XCTAssertThrowsError(try Firmware.plan(keyboard: keyboard("urchin"), reset: false, files: firmwareFolder())) {
            guard case FlashError.firmwareNotFound = $0 else { return XCTFail("\($0)") }
        }
    }

    func testNewestDownload() throws {
        for (name, age) in [("firmware.zip", 300.0), ("firmware (2).zip", 100.0), ("other.zip", 0.0)] {
            let file = scratch.appendingPathComponent(name)
            try Data().write(to: file)
            try FileManager.default.setAttributes(
                [.modificationDate: Date().addingTimeInterval(-age)], ofItemAtPath: file.path)
        }
        XCTAssertEqual(Firmware.newestDownload(in: scratch)?.lastPathComponent, "firmware (2).zip")
    }

    func testFlashCopiesOntoTheDrive() async throws {
        let step = try Firmware.plan(keyboard: keyboard("waterfowl"), only: ["left"], reset: false, files: firmwareFolder())[0]
        let volumes = scratch.appendingPathComponent("volumes")
        try FileManager.default.createDirectory(at: volumes, withIntermediateDirectories: true)
        let seen = Recorder()

        let nano = board("UF2 Bootloader\nModel: nice!nano\nBoard-ID: nRF52840-nicenano", in: volumes)
        try await flash(step, roots: [volumes], boards: targets.boards, timing: fast) { await seen.add($0) }

        let copied = try await nano.value
        XCTAssertEqual(copied, step.uf2.lastPathComponent)
        let statuses = await seen.statuses
        XCTAssertEqual(statuses.suffix(3), [.copying, .restarting, .done])
    }

    func testWrongBoardIsLeftAlone() async throws {
        let step = try Firmware.plan(keyboard: keyboard("waterfowl"), only: ["left"], reset: false, files: firmwareFolder())[0]
        let volumes = scratch.appendingPathComponent("volumes")
        let xiao = volumes.appendingPathComponent("XIAO-SENSE")
        try FileManager.default.createDirectory(at: xiao, withIntermediateDirectories: true)
        try Data("Model: Seeed XIAO nRF52840".utf8).write(to: xiao.appendingPathComponent("INFO_UF2.TXT"))
        let seen = Recorder()

        let flashing = Task {
            try await flash(step, roots: [volumes], boards: targets.boards, timing: fast) { await seen.add($0) }
        }
        try await Task.sleep(nanoseconds: 200_000_000)
        var statuses = await seen.statuses
        XCTAssertEqual(statuses, [.wrongBoard("xiao_ble")])
        XCTAssertEqual(try FileManager.default.contentsOfDirectory(atPath: xiao.path), ["INFO_UF2.TXT"])

        // Swap it for the right board and it carries on.
        try FileManager.default.removeItem(at: xiao)
        let nano = board("Model: nice!nano", in: volumes, named: "NICENANO")
        try await flashing.value
        let copied = try await nano.value
        XCTAssertEqual(copied, step.uf2.lastPathComponent)
        statuses = await seen.statuses
        XCTAssertEqual(statuses.last, .done)
    }

    func testDriveThatStaysIsAnError() async throws {
        let step = try Firmware.plan(keyboard: keyboard("waterfowl"), only: ["left"], reset: false, files: firmwareFolder())[0]
        let drive = scratch.appendingPathComponent("volumes/NICENANO")
        try FileManager.default.createDirectory(at: drive, withIntermediateDirectories: true)
        try Data("Model: nice!nano".utf8).write(to: drive.appendingPathComponent("INFO_UF2.TXT"))
        do {
            try await flash(
                step, roots: [drive.deletingLastPathComponent()], boards: targets.boards,
                timing: FlashTiming(poll: 0.02, settle: 0, restart: 0.1)) { _ in }
            XCTFail("should have thrown")
        } catch {
            XCTAssertEqual(error as? FlashError, .stillMounted("NICENANO"))
        }
    }

    /// Every device's firmware name must be something build.yaml builds.
    func testTargetsMatchTheBuildMatrix() throws {
        let matrix = try String(
            contentsOf: Self.tools.deletingLastPathComponent().appendingPathComponent("build.yaml"), encoding: .utf8)
        var built: Set<String> = []
        for line in matrix.split(separator: "\n") {
            let words = line.split(separator: " ")
            if words.count >= 2, ["shield:", "artifact-name:"].contains(words[0]) || words[0] == "-" && words[1] == "shield:" {
                built.insert(String(words[0] == "-" ? words[2] : words[1]))
            }
        }
        XCTAssertTrue(built.contains(Firmware.settingsReset))
        for device in targets.keyboards.flatMap(\.devices) {
            XCTAssertTrue(built.contains(device.firmware), "\(device.firmware) is not in build.yaml")
            XCTAssertNotNil(targets.boards[device.board], "\(device.board) is not in targets.json's boards")
        }
    }
}

actor Recorder {
    var statuses: [StepStatus] = []
    func add(_ status: StepStatus) { statuses.append(status) }
}
