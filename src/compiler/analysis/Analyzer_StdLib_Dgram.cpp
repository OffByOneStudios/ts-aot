#include "Analyzer.h"

namespace ts {

void Analyzer::registerDgram() {
    auto dgramModule = std::make_shared<ObjectType>();

    // UDPSocket class for UDP sockets (using distinct name to avoid conflicts with net.Socket)
    auto udpSocketClass = std::make_shared<ClassType>("UDPSocket");

    // Get EventEmitter as base class
    auto eventEmitterClass = std::static_pointer_cast<ClassType>(symbols.lookupType("EventEmitter"));
    if (eventEmitterClass) {
        udpSocketClass->baseClass = eventEmitterClass;
    }

    // socket.bind(port, address?, callback?) -> void
    auto bindMethod = std::make_shared<FunctionType>();
    bindMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));     // port
    bindMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));  // address (optional)
    bindMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Function)); // callback (optional)
    bindMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    udpSocketClass->methods["bind"] = bindMethod;

    // socket.send(msg, offset?, length?, port, address, callback?) -> void
    auto sendMethod = std::make_shared<FunctionType>();
    sendMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));     // msg (Buffer or string)
    sendMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));     // offset (optional)
    sendMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));     // length (optional)
    sendMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));     // port
    sendMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));  // address
    sendMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Function)); // callback (optional)
    sendMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    udpSocketClass->methods["send"] = sendMethod;

    // socket.close(callback?) -> void
    auto closeMethod = std::make_shared<FunctionType>();
    closeMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Function)); // callback (optional)
    closeMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    udpSocketClass->methods["close"] = closeMethod;

    // AddressInfo type for address() return value: { address: string, family: string, port: number }
    auto addressInfoType = std::make_shared<ObjectType>();
    addressInfoType->fields["address"] = std::make_shared<Type>(TypeKind::String);
    addressInfoType->fields["family"] = std::make_shared<Type>(TypeKind::String);
    addressInfoType->fields["port"] = std::make_shared<Type>(TypeKind::Int);

    // socket.address() -> { address, family, port }
    auto addressMethod = std::make_shared<FunctionType>();
    addressMethod->returnType = addressInfoType;
    udpSocketClass->methods["address"] = addressMethod;

    // socket.setBroadcast(flag: boolean) -> void
    auto setBroadcastMethod = std::make_shared<FunctionType>();
    setBroadcastMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Boolean));
    setBroadcastMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    udpSocketClass->methods["setBroadcast"] = setBroadcastMethod;

    // socket.setMulticastTTL(ttl: number) -> void
    auto setMulticastTTLMethod = std::make_shared<FunctionType>();
    setMulticastTTLMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    setMulticastTTLMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    udpSocketClass->methods["setMulticastTTL"] = setMulticastTTLMethod;

    // socket.setMulticastLoopback(flag: boolean) -> void
    auto setMulticastLoopbackMethod = std::make_shared<FunctionType>();
    setMulticastLoopbackMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Boolean));
    setMulticastLoopbackMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    udpSocketClass->methods["setMulticastLoopback"] = setMulticastLoopbackMethod;

    // socket.setMulticastInterface(interfaceAddress: string) -> void
    auto setMulticastInterfaceMethod = std::make_shared<FunctionType>();
    setMulticastInterfaceMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    setMulticastInterfaceMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    udpSocketClass->methods["setMulticastInterface"] = setMulticastInterfaceMethod;

    // socket.addMembership(multicastAddress: string, multicastInterface?: string) -> void
    auto addMembershipMethod = std::make_shared<FunctionType>();
    addMembershipMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));  // multicastAddress
    addMembershipMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));  // multicastInterface (optional)
    addMembershipMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    udpSocketClass->methods["addMembership"] = addMembershipMethod;

    // socket.dropMembership(multicastAddress: string, multicastInterface?: string) -> void
    auto dropMembershipMethod = std::make_shared<FunctionType>();
    dropMembershipMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));  // multicastAddress
    dropMembershipMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));  // multicastInterface (optional)
    dropMembershipMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    udpSocketClass->methods["dropMembership"] = dropMembershipMethod;

    // socket.connect(port: number, address?: string, callback?: Function) -> void
    auto connectMethod = std::make_shared<FunctionType>();
    connectMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));     // port
    connectMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));  // address (optional)
    connectMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Function)); // callback (optional)
    connectMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    udpSocketClass->methods["connect"] = connectMethod;

    // socket.disconnect() -> void
    auto disconnectMethod = std::make_shared<FunctionType>();
    disconnectMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    udpSocketClass->methods["disconnect"] = disconnectMethod;

    // socket.ref() -> Socket
    auto refMethod = std::make_shared<FunctionType>();
    refMethod->returnType = udpSocketClass;
    udpSocketClass->methods["ref"] = refMethod;

    // socket.unref() -> Socket
    auto unrefMethod = std::make_shared<FunctionType>();
    unrefMethod->returnType = udpSocketClass;
    udpSocketClass->methods["unref"] = unrefMethod;

    // socket.setRecvBufferSize(size: number) -> void
    auto setRecvBufferSizeMethod = std::make_shared<FunctionType>();
    setRecvBufferSizeMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    setRecvBufferSizeMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    udpSocketClass->methods["setRecvBufferSize"] = setRecvBufferSizeMethod;

    // socket.setSendBufferSize(size: number) -> void
    auto setSendBufferSizeMethod = std::make_shared<FunctionType>();
    setSendBufferSizeMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    setSendBufferSizeMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    udpSocketClass->methods["setSendBufferSize"] = setSendBufferSizeMethod;

    // socket.getRecvBufferSize() -> number
    auto getRecvBufferSizeMethod = std::make_shared<FunctionType>();
    getRecvBufferSizeMethod->returnType = std::make_shared<Type>(TypeKind::Int);
    udpSocketClass->methods["getRecvBufferSize"] = getRecvBufferSizeMethod;

    // socket.getSendBufferSize() -> number
    auto getSendBufferSizeMethod = std::make_shared<FunctionType>();
    getSendBufferSizeMethod->returnType = std::make_shared<Type>(TypeKind::Int);
    udpSocketClass->methods["getSendBufferSize"] = getSendBufferSizeMethod;

    // socket.setTTL(ttl: number) -> void
    auto setTTLMethod = std::make_shared<FunctionType>();
    setTTLMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    setTTLMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    udpSocketClass->methods["setTTL"] = setTTLMethod;

    // socket.remoteAddress() -> { address, family, port }
    auto remoteAddressMethod = std::make_shared<FunctionType>();
    remoteAddressMethod->returnType = addressInfoType;  // Reuse AddressInfo type
    udpSocketClass->methods["remoteAddress"] = remoteAddressMethod;

    // Socket instance properties
    udpSocketClass->fields["remoteAddress"] = std::make_shared<Type>(TypeKind::String);
    udpSocketClass->fields["remotePort"] = std::make_shared<Type>(TypeKind::Int);

    symbols.defineType("UDPSocket", udpSocketClass);
    dgramModule->fields["Socket"] = udpSocketClass;

    // dgram.createSocket(type: 'udp4' | 'udp6', callback?: Function) -> Socket
    auto createSocketType = std::make_shared<FunctionType>();
    createSocketType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));  // type
    createSocketType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Function)); // callback (optional)
    createSocketType->returnType = udpSocketClass;
    dgramModule->fields["createSocket"] = createSocketType;

    // Register the dgram module
    symbols.define("dgram", dgramModule);
}

} // namespace ts
