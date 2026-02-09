#include "liva/AST/Type.h"
#include <gtest/gtest.h>

using namespace liva;

TEST(TypeTest, PrimitiveTypes) {
    auto i32 = makeI32Type();
    EXPECT_TRUE(i32->isInteger());
    EXPECT_TRUE(i32->isSignedInteger());
    EXPECT_FALSE(i32->isFloat());
    EXPECT_TRUE(i32->isNumeric());
    EXPECT_TRUE(i32->isPrimitive());
    EXPECT_EQ(i32->getBitWidth(), 32);
    EXPECT_EQ(i32->toString(), "i32");
}

TEST(TypeTest, FloatTypes) {
    auto f64 = makeF64Type();
    EXPECT_FALSE(f64->isInteger());
    EXPECT_TRUE(f64->isFloat());
    EXPECT_TRUE(f64->isNumeric());
    EXPECT_EQ(f64->getBitWidth(), 64);
    EXPECT_EQ(f64->toString(), "f64");
}

TEST(TypeTest, BoolType) {
    auto b = makeBoolType();
    EXPECT_TRUE(b->isBool());
    EXPECT_FALSE(b->isInteger());
    EXPECT_TRUE(b->isPrimitive());
    EXPECT_EQ(b->getBitWidth(), 1);
    EXPECT_EQ(b->toString(), "bool");
}

TEST(TypeTest, VoidType) {
    auto v = makeVoidType();
    EXPECT_TRUE(v->isVoid());
    EXPECT_FALSE(v->isNumeric());
    EXPECT_EQ(v->toString(), "void");
}

TEST(TypeTest, StringType) {
    auto s = makeStringType();
    EXPECT_TRUE(s->isPrimitive());
    EXPECT_EQ(s->toString(), "string");
}

TEST(TypeTest, NamedType) {
    auto t = makeNamedType("Point");
    EXPECT_EQ(t->getKind(), TypeRepr::Kind::Named);
    EXPECT_EQ(t->toString(), "Point");
}

TEST(TypeTest, ReferenceType) {
    auto inner = makeI32Type();
    auto ref = std::make_unique<ReferenceTypeRepr>(std::move(inner), false);
    EXPECT_TRUE(ref->isReference());
    EXPECT_FALSE(ref->isMutable());
    EXPECT_EQ(ref->toString(), "ref i32");

    auto inner2 = makeI32Type();
    auto mutRef = std::make_unique<ReferenceTypeRepr>(std::move(inner2), true);
    EXPECT_TRUE(mutRef->isMutable());
    EXPECT_EQ(mutRef->toString(), "ref mut i32");
}

TEST(TypeTest, ArrayType) {
    auto elem = makeI32Type();
    auto arr = std::make_unique<ArrayTypeRepr>(std::move(elem), 10);
    EXPECT_FALSE(arr->isDynamic());
    EXPECT_EQ(arr->getSize(), 10);
    EXPECT_EQ(arr->toString(), "[i32; 10]");

    auto elem2 = makeI32Type();
    auto dynArr = std::make_unique<ArrayTypeRepr>(std::move(elem2));
    EXPECT_TRUE(dynArr->isDynamic());
    EXPECT_EQ(dynArr->toString(), "[i32]");
}

TEST(TypeTest, OptionalType) {
    auto inner = makeI32Type();
    auto opt = std::make_unique<OptionalTypeRepr>(std::move(inner));
    EXPECT_EQ(opt->toString(), "i32?");
}

TEST(TypeTest, GenericType) {
    std::vector<std::unique_ptr<TypeRepr>> args;
    args.push_back(makeI32Type());
    auto gen = std::make_unique<GenericTypeRepr>("Vec", std::move(args));
    EXPECT_EQ(gen->toString(), "Vec<i32>");
}

TEST(TypeTest, InferredType) {
    auto inf = makeInferredType();
    EXPECT_TRUE(inf->isInferred());
}

TEST(TypeTest, AllPrimitiveBitWidths) {
    EXPECT_EQ(makePrimitiveType(TypeRepr::Kind::I8)->getBitWidth(), 8);
    EXPECT_EQ(makePrimitiveType(TypeRepr::Kind::I16)->getBitWidth(), 16);
    EXPECT_EQ(makePrimitiveType(TypeRepr::Kind::I32)->getBitWidth(), 32);
    EXPECT_EQ(makePrimitiveType(TypeRepr::Kind::I64)->getBitWidth(), 64);
    EXPECT_EQ(makePrimitiveType(TypeRepr::Kind::U8)->getBitWidth(), 8);
    EXPECT_EQ(makePrimitiveType(TypeRepr::Kind::U16)->getBitWidth(), 16);
    EXPECT_EQ(makePrimitiveType(TypeRepr::Kind::U32)->getBitWidth(), 32);
    EXPECT_EQ(makePrimitiveType(TypeRepr::Kind::U64)->getBitWidth(), 64);
    EXPECT_EQ(makePrimitiveType(TypeRepr::Kind::F32)->getBitWidth(), 32);
    EXPECT_EQ(makePrimitiveType(TypeRepr::Kind::F64)->getBitWidth(), 64);
}
