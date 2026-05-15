# --- 第一阶段：构建环境 ---
FROM alpine:3.20 AS builder

RUN sed -i 's/dl-cdn.alpinelinux.org/mirrors.aliyun.com/g' /etc/apk/repositories && \
    apk update && \
    apk add --no-cache \
    build-base \
    cmake \
    grpc-dev \
    grpc-plugins \
    protobuf-dev \
    curl-dev \
    hiredis-dev \
    nlohmann-json \
    linux-headers

WORKDIR /build
COPY . .

# 编译项目
RUN rm -rf build && mkdir -p build && cd build && \
    cmake .. && \
    make -j$(nproc)

# --- 第二阶段：运行环境 ---
FROM alpine:3.20

RUN sed -i 's/dl-cdn.alpinelinux.org/mirrors.aliyun.com/g' /etc/apk/repositories && \
    apk update && \
    # 仅安装最基础的运行时，其他库我们从 builder 搬运
    apk add --no-cache libstdc++ hiredis ca-certificates

WORKDIR /app

# 1. 拷贝二进制
COPY --from=builder /build/build/bin/pokemon_server /app/pokemon_server

# 拷贝数据文件
COPY --from=builder /build/src/simulator/data/ /app/data/

# 2. 【核武器级拷贝】直接同步所有的库目录
# 既然不知道它在哪，我们就把 builder 里的所有库都拷过来
COPY --from=builder /usr/lib/ /usr/lib/
COPY --from=builder /lib/ /lib/

RUN chmod +x /app/pokemon_server

# 3. 【自动纠错】如果它在找 /usr/local/lib，我们做一个软链接
RUN [ -d /usr/local/lib ] || mkdir -p /usr/local/lib
COPY --from=builder /usr/local/lib/ /usr/local/lib/

# 最终验证：如果 ldd 还是说 not found，构建会直接失败
RUN ldd /app/pokemon_server

CMD ["/app/pokemon_server"]