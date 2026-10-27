import AVFoundation

final class Tone: NSObject, AVAudioPlayerDelegate {
  private var player: AVAudioPlayer?
  func play(done: Bool) {
    let count = 6615
    let rate = 44100
    let frequency = done ? 1320.0 : 880.0
    var wav = Data()
    func word<T: FixedWidthInteger>(_ value: T) {
      var value = value.littleEndian
      withUnsafeBytes(of: &value) { wav.append(contentsOf: $0) }
    }
    wav.append(Data("RIFF".utf8))
    word(UInt32(36 + count * 2))
    wav.append(Data("WAVEfmt ".utf8))
    word(UInt32(16))
    word(UInt16(1))
    word(UInt16(1))
    word(UInt32(rate))
    word(UInt32(rate * 2))
    word(UInt16(2))
    word(UInt16(16))
    wav.append(Data("data".utf8))
    word(UInt32(count * 2))
    for i in 0..<count {
      let envelope = min(1, Double(i) / 200, Double(count - i) / 200)
      word(Int16(6000 * envelope * sin(2 * .pi * frequency * Double(i) / Double(rate))))
    }
    try? AVAudioSession.sharedInstance().setCategory(.playback, options: .mixWithOthers)
    try? AVAudioSession.sharedInstance().setActive(true)
    player = try? AVAudioPlayer(data: wav)
    player?.delegate = self
    player?.play()
  }
  func audioPlayerDidFinishPlaying(_ player: AVAudioPlayer, successfully flag: Bool) { stop() }
  func stop() {
    player?.stop()
    try? AVAudioSession.sharedInstance().setActive(false, options: .notifyOthersOnDeactivation)
  }
}
