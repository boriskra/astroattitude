FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends g++ cmake ninja-build python3 git ca-certificates && rm -rf /var/lib/apt/lists/*
WORKDIR /source
COPY . .
RUN python3 tools/fetch_dependencies.py && cmake -S . -B /native -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build /native -j12
CMD ["sh", "-c", "c++ --version && uname -a && ctest --test-dir /native --output-on-failure -j12 && PYTHONPATH=/source/bindings/python python3 examples/python_synthetic.py /native/libastroattitude_c.so /native/fixtures"]
