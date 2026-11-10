import FlasherCore
import SwiftUI

/// The keyboard drawn roughly as it sits on the desk: dongle on top, halves below.
struct KeyboardPicture: View {
    let devices: [Device]
    let look: (Device) -> DeviceTile.Look
    var tap: ((Device) -> Void)?

    var body: some View {
        VStack(spacing: 14) {
            ForEach(devices.filter { $0.name == "dongle" }) { tile($0) }
            HStack(alignment: .top, spacing: 36) {
                ForEach(devices.filter { $0.name != "dongle" }) { tile($0) }
            }
        }
    }

    @ViewBuilder
    private func tile(_ device: Device) -> some View {
        if let tap {
            Button { tap(device) } label: { DeviceTile(device: device, look: look(device)) }
                .buttonStyle(.plain)
                .help("Click to leave the \(device.name) out, or put it back")
        } else {
            DeviceTile(device: device, look: look(device))
        }
    }
}

struct DeviceTile: View {
    enum Look {
        case off  // not being flashed
        case idle
        case next  // the one to plug in now
        case done
    }

    let device: Device
    let look: Look

    var body: some View {
        VStack(spacing: 12) {
            Group {
                if look == .next {
                    picture.modifier(Pulse())
                } else {
                    picture
                }
            }
            .overlay(alignment: .topTrailing) {
                if look == .done {
                    Image(systemName: "checkmark.circle.fill")
                        .font(.title2)
                        .foregroundStyle(.white, .green)
                        .offset(x: 8, y: -8)
                        .transition(.scale.combined(with: .opacity))
                }
            }
            Text(device.name.capitalized)
                .font(.callout.weight(look == .next ? .semibold : .regular))
                .foregroundStyle(look == .next ? Color.accentColor : Color.secondary)
        }
        .opacity(look == .off ? 0.3 : 1)
        .contentShape(Rectangle())
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(device.what)
    }

    private var tint: Color {
        switch look {
        case .next: return .accentColor
        case .done: return .green
        case .off, .idle: return .secondary
        }
    }

    @ViewBuilder
    private var picture: some View {
        if device.name == "dongle" {
            DonglePicture(tint: tint)
        } else {
            HalfPicture(tint: tint, right: device.name == "right")
        }
    }
}

/// Gentle throb for the device that needs attention.
private struct Pulse: ViewModifier {
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    @State private var on = false

    func body(content: Content) -> some View {
        content
            .scaleEffect(on ? 1.05 : 1)
            .shadow(color: Color.accentColor.opacity(on ? 0.65 : 0.2), radius: on ? 18 : 8)
            .onAppear {
                guard !reduceMotion else { return }
                withAnimation(.easeInOut(duration: 0.85).repeatForever(autoreverses: true)) { on = true }
            }
    }
}

/// One half: three rows of five keys and two thumb keys on the inner side.
struct HalfPicture: View {
    let tint: Color
    let right: Bool

    var body: some View {
        VStack(alignment: right ? .leading : .trailing, spacing: 5) {
            ForEach(0..<3, id: \.self) { _ in
                HStack(spacing: 5) {
                    ForEach(0..<5, id: \.self) { _ in key }
                }
            }
            HStack(spacing: 5) {
                key
                key
            }
            .padding(.top, 4)
        }
        .padding(12)
        .background(RoundedRectangle(cornerRadius: 16, style: .continuous).fill(tint.opacity(0.16)))
        .overlay(RoundedRectangle(cornerRadius: 16, style: .continuous).strokeBorder(tint.opacity(0.5), lineWidth: 1.5))
        .rotationEffect(.degrees(right ? -7 : 7))
        .padding(8)
    }

    private var key: some View {
        RoundedRectangle(cornerRadius: 4, style: .continuous)
            .fill(tint.opacity(0.75))
            .frame(width: 18, height: 18)
    }
}

/// The dongle: a small box with a screen and its USB cable.
struct DonglePicture: View {
    let tint: Color

    var body: some View {
        VStack(spacing: 0) {
            Capsule().fill(tint.opacity(0.5)).frame(width: 4, height: 12)
            RoundedRectangle(cornerRadius: 5, style: .continuous)
                .fill(tint.opacity(0.75))
                .frame(width: 58, height: 26)
                .padding(8)
                .background(RoundedRectangle(cornerRadius: 12, style: .continuous).fill(tint.opacity(0.16)))
                .overlay(RoundedRectangle(cornerRadius: 12, style: .continuous).strokeBorder(tint.opacity(0.5), lineWidth: 1.5))
        }
    }
}
