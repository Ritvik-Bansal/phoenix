import SwiftUI

/// The 72x40 monochrome display, scaled up. Same font bytes and same layout
/// rules as the firmware, so what shows here is what the wearer sees.
struct GlassesView: View {
    let frameBuffer: GlassesFrameBuffer
    var brightness: UInt8 = 255
    var scale: CGFloat = 4

    private let phosphor = Color(red: 0.49, green: 1.0, blue: 0.69)

    var body: some View {
        Canvas { context, _ in
            let on = phosphor.opacity(0.25 + 0.75 * Double(brightness) / 255)
            for y in 0..<GlassesFrameBuffer.height {
                var x = 0
                while x < GlassesFrameBuffer.width {
                    guard frameBuffer.get(x, y) else { x += 1; continue }
                    // Coalesce horizontal runs into one rect.
                    var run = 1
                    while x + run < GlassesFrameBuffer.width && frameBuffer.get(x + run, y) { run += 1 }
                    context.fill(
                        Path(CGRect(x: CGFloat(x) * scale, y: CGFloat(y) * scale,
                                    width: CGFloat(run) * scale, height: scale)),
                        with: .color(on))
                    x += run
                }
            }
        }
        .frame(width: CGFloat(GlassesFrameBuffer.width) * scale,
               height: CGFloat(GlassesFrameBuffer.height) * scale)
        .background(Color.black)
        .clipShape(RoundedRectangle(cornerRadius: 6))
        .overlay(
            RoundedRectangle(cornerRadius: 6)
                .stroke(Color.white.opacity(0.08), lineWidth: 1))
        .shadow(color: phosphor.opacity(0.25), radius: 8)
        .accessibilityLabel("Virtual glasses display")
    }
}

#Preview {
    let sim = GlassesSimulation()
    sim.apply(PhoenixMessages.assistantText("Sunny with a high of 22C. Fog after sunset."))
    return GlassesView(frameBuffer: sim.render())
        .padding()
        .background(Color.black)
}
