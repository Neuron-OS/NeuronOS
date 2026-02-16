# Homebrew formula for NeuronOS
# Install: brew tap Neuron-OS/neuronos && brew install neuronos-cli
#
# This file is meant to live in a separate repo:
#   github.com/Neuron-OS/homebrew-neuronos/Formula/neuronos-cli.rb
#
class NeuronosCli < Formula
  desc "Sovereign AI agent runtime — inference, tools, memory in one binary"
  homepage "https://github.com/Neuron-OS/neuronos"
  url "https://github.com/Neuron-OS/neuronos/archive/refs/tags/v0.9.1.tar.gz"
  sha256 "REPLACE_WITH_ACTUAL_SHA256_AFTER_TAGGING"
  license "MIT"

  depends_on "cmake" => :build

  def install
    system "cmake", "-S", ".", "-B", "build",
           "-DCMAKE_BUILD_TYPE=Release",
           "-DBUILD_SHARED_LIBS=OFF",
           "-DBITNET_X86_TL2=OFF",
           *std_cmake_args
    system "cmake", "--build", "build", "--config", "Release", "-j#{ENV.make_jobs}"
    bin.install "build/bin/neuronos-cli"

    # Install grammars
    (share/"neuronos/grammars").install Dir["neuronos/grammars/*.gbnf"]
  end

  test do
    assert_match "NeuronOS", shell_output("#{bin}/neuronos-cli --help")
  end
end
