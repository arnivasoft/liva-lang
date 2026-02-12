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

// ============================================================
// Unsigned Integer Types
// ============================================================

TEST(TypeTest, U8Type) {
    auto u8 = makePrimitiveType(TypeRepr::Kind::U8);
    EXPECT_TRUE(u8->isInteger());
    EXPECT_TRUE(u8->isUnsignedInteger());
    EXPECT_FALSE(u8->isSignedInteger());
    EXPECT_FALSE(u8->isFloat());
    EXPECT_TRUE(u8->isNumeric());
    EXPECT_TRUE(u8->isPrimitive());
    EXPECT_EQ(u8->getBitWidth(), 8);
    EXPECT_EQ(u8->toString(), "u8");
}

TEST(TypeTest, U16Type) {
    auto u16 = makePrimitiveType(TypeRepr::Kind::U16);
    EXPECT_TRUE(u16->isInteger());
    EXPECT_TRUE(u16->isUnsignedInteger());
    EXPECT_FALSE(u16->isSignedInteger());
    EXPECT_FALSE(u16->isFloat());
    EXPECT_TRUE(u16->isNumeric());
    EXPECT_TRUE(u16->isPrimitive());
    EXPECT_EQ(u16->getBitWidth(), 16);
    EXPECT_EQ(u16->toString(), "u16");
}

TEST(TypeTest, U32Type) {
    auto u32 = makePrimitiveType(TypeRepr::Kind::U32);
    EXPECT_TRUE(u32->isInteger());
    EXPECT_TRUE(u32->isUnsignedInteger());
    EXPECT_FALSE(u32->isSignedInteger());
    EXPECT_FALSE(u32->isFloat());
    EXPECT_TRUE(u32->isNumeric());
    EXPECT_TRUE(u32->isPrimitive());
    EXPECT_EQ(u32->getBitWidth(), 32);
    EXPECT_EQ(u32->toString(), "u32");
}

TEST(TypeTest, U64Type) {
    auto u64 = makePrimitiveType(TypeRepr::Kind::U64);
    EXPECT_TRUE(u64->isInteger());
    EXPECT_TRUE(u64->isUnsignedInteger());
    EXPECT_FALSE(u64->isSignedInteger());
    EXPECT_FALSE(u64->isFloat());
    EXPECT_TRUE(u64->isNumeric());
    EXPECT_TRUE(u64->isPrimitive());
    EXPECT_EQ(u64->getBitWidth(), 64);
    EXPECT_EQ(u64->toString(), "u64");
}

// ============================================================
// Signed Integer Types (I8, I16, I64 not individually tested above)
// ============================================================

TEST(TypeTest, I8Type) {
    auto i8 = makePrimitiveType(TypeRepr::Kind::I8);
    EXPECT_TRUE(i8->isInteger());
    EXPECT_TRUE(i8->isSignedInteger());
    EXPECT_FALSE(i8->isUnsignedInteger());
    EXPECT_FALSE(i8->isFloat());
    EXPECT_TRUE(i8->isNumeric());
    EXPECT_TRUE(i8->isPrimitive());
    EXPECT_EQ(i8->getBitWidth(), 8);
    EXPECT_EQ(i8->toString(), "i8");
}

TEST(TypeTest, I16Type) {
    auto i16 = makePrimitiveType(TypeRepr::Kind::I16);
    EXPECT_TRUE(i16->isInteger());
    EXPECT_TRUE(i16->isSignedInteger());
    EXPECT_FALSE(i16->isUnsignedInteger());
    EXPECT_FALSE(i16->isFloat());
    EXPECT_TRUE(i16->isNumeric());
    EXPECT_TRUE(i16->isPrimitive());
    EXPECT_EQ(i16->getBitWidth(), 16);
    EXPECT_EQ(i16->toString(), "i16");
}

TEST(TypeTest, I64Type) {
    auto i64 = makeI64Type();
    EXPECT_TRUE(i64->isInteger());
    EXPECT_TRUE(i64->isSignedInteger());
    EXPECT_FALSE(i64->isUnsignedInteger());
    EXPECT_FALSE(i64->isFloat());
    EXPECT_TRUE(i64->isNumeric());
    EXPECT_TRUE(i64->isPrimitive());
    EXPECT_EQ(i64->getBitWidth(), 64);
    EXPECT_EQ(i64->toString(), "i64");
}

// ============================================================
// F32 Type
// ============================================================

TEST(TypeTest, F32Type) {
    auto f32 = makePrimitiveType(TypeRepr::Kind::F32);
    EXPECT_TRUE(f32->isFloat());
    EXPECT_FALSE(f32->isInteger());
    EXPECT_FALSE(f32->isSignedInteger());
    EXPECT_FALSE(f32->isUnsignedInteger());
    EXPECT_TRUE(f32->isNumeric());
    EXPECT_TRUE(f32->isPrimitive());
    EXPECT_EQ(f32->getBitWidth(), 32);
    EXPECT_EQ(f32->toString(), "f32");
}

// ============================================================
// Function Type
// ============================================================

TEST(TypeTest, FunctionTypeSingleParam) {
    std::vector<std::unique_ptr<TypeRepr>> params;
    params.push_back(makeI32Type());
    auto ret = makeI32Type();
    auto fn = std::make_unique<FunctionTypeRepr>(std::move(params), std::move(ret));

    EXPECT_EQ(fn->getKind(), TypeRepr::Kind::Function);
    EXPECT_EQ(fn->getParams().size(), 1u);
    EXPECT_EQ(fn->getParams()[0]->toString(), "i32");
    EXPECT_EQ(fn->getReturnType()->toString(), "i32");
    EXPECT_EQ(fn->toString(), "(i32) -> i32");
}

TEST(TypeTest, FunctionTypeMultiParam) {
    std::vector<std::unique_ptr<TypeRepr>> params;
    params.push_back(makeI32Type());
    params.push_back(makeStringType());
    auto ret = makeBoolType();
    auto fn = std::make_unique<FunctionTypeRepr>(std::move(params), std::move(ret));

    EXPECT_EQ(fn->getParams().size(), 2u);
    EXPECT_EQ(fn->getParams()[0]->toString(), "i32");
    EXPECT_EQ(fn->getParams()[1]->toString(), "string");
    EXPECT_EQ(fn->getReturnType()->toString(), "bool");
    EXPECT_EQ(fn->toString(), "(i32, string) -> bool");
}

TEST(TypeTest, FunctionTypeNoParams) {
    std::vector<std::unique_ptr<TypeRepr>> params;
    auto ret = makeVoidType();
    auto fn = std::make_unique<FunctionTypeRepr>(std::move(params), std::move(ret));

    EXPECT_EQ(fn->getParams().size(), 0u);
    EXPECT_EQ(fn->getReturnType()->toString(), "void");
    EXPECT_EQ(fn->toString(), "() -> void");
}

// ============================================================
// Tuple Type
// ============================================================

TEST(TypeTest, TupleTypePair) {
    std::vector<std::unique_ptr<TypeRepr>> elems;
    elems.push_back(makeI32Type());
    elems.push_back(makeStringType());
    auto tup = std::make_unique<TupleTypeRepr>(std::move(elems));

    EXPECT_EQ(tup->getKind(), TypeRepr::Kind::Tuple);
    EXPECT_EQ(tup->getArity(), 2u);
    EXPECT_EQ(tup->getElements()[0]->toString(), "i32");
    EXPECT_EQ(tup->getElements()[1]->toString(), "string");
    EXPECT_EQ(tup->toString(), "(i32, string)");
}

TEST(TypeTest, TupleTypeTriple) {
    std::vector<std::unique_ptr<TypeRepr>> elems;
    elems.push_back(makeI32Type());
    elems.push_back(makeF64Type());
    elems.push_back(makeBoolType());
    auto tup = std::make_unique<TupleTypeRepr>(std::move(elems));

    EXPECT_EQ(tup->getArity(), 3u);
    EXPECT_EQ(tup->getElements()[0]->toString(), "i32");
    EXPECT_EQ(tup->getElements()[1]->toString(), "f64");
    EXPECT_EQ(tup->getElements()[2]->toString(), "bool");
    EXPECT_EQ(tup->toString(), "(i32, f64, bool)");
}

TEST(TypeTest, TupleTypeSingleElement) {
    std::vector<std::unique_ptr<TypeRepr>> elems;
    elems.push_back(makeI32Type());
    auto tup = std::make_unique<TupleTypeRepr>(std::move(elems));

    EXPECT_EQ(tup->getArity(), 1u);
    EXPECT_EQ(tup->toString(), "(i32)");
}

// ============================================================
// Result Type
// ============================================================

TEST(TypeTest, ResultTypeI32String) {
    auto ok = makeI32Type();
    auto err = makeStringType();
    auto res = std::make_unique<ResultTypeRepr>(std::move(ok), std::move(err));

    EXPECT_EQ(res->getKind(), TypeRepr::Kind::Result);
    EXPECT_EQ(res->getOkType()->toString(), "i32");
    EXPECT_EQ(res->getErrType()->toString(), "string");
    EXPECT_EQ(res->toString(), "Result<i32, string>");
}

TEST(TypeTest, ResultTypeBoolI32) {
    auto ok = makeBoolType();
    auto err = makeI32Type();
    auto res = std::make_unique<ResultTypeRepr>(std::move(ok), std::move(err));

    EXPECT_EQ(res->getOkType()->toString(), "bool");
    EXPECT_EQ(res->getErrType()->toString(), "i32");
    EXPECT_EQ(res->toString(), "Result<bool, i32>");
}

// ============================================================
// Nested Types
// ============================================================

TEST(TypeTest, OptionalOfArray) {
    // [i32]?
    auto elem = makeI32Type();
    auto arr = std::make_unique<ArrayTypeRepr>(std::move(elem));
    auto opt = std::make_unique<OptionalTypeRepr>(std::move(arr));

    EXPECT_EQ(opt->getKind(), TypeRepr::Kind::Optional);
    EXPECT_EQ(opt->getInner()->getKind(), TypeRepr::Kind::Array);
    EXPECT_EQ(opt->toString(), "[i32]?");
}

TEST(TypeTest, ArrayOfOptional) {
    // [i32?; 5]
    auto inner = makeI32Type();
    auto optElem = std::make_unique<OptionalTypeRepr>(std::move(inner));
    auto arr = std::make_unique<ArrayTypeRepr>(std::move(optElem), 5);

    EXPECT_EQ(arr->getKind(), TypeRepr::Kind::Array);
    EXPECT_EQ(arr->getElement()->getKind(), TypeRepr::Kind::Optional);
    EXPECT_EQ(arr->getSize(), 5);
    EXPECT_FALSE(arr->isDynamic());
    EXPECT_EQ(arr->toString(), "[i32?; 5]");
}

TEST(TypeTest, ReferenceToArray) {
    // ref [i32]
    auto elem = makeI32Type();
    auto arr = std::make_unique<ArrayTypeRepr>(std::move(elem));
    auto ref = std::make_unique<ReferenceTypeRepr>(std::move(arr), false);

    EXPECT_TRUE(ref->isReference());
    EXPECT_FALSE(ref->isMutable());
    EXPECT_EQ(ref->getInner()->getKind(), TypeRepr::Kind::Array);
    EXPECT_EQ(ref->toString(), "ref [i32]");
}

TEST(TypeTest, GenericMultipleArgs) {
    // Map<string, i32>
    std::vector<std::unique_ptr<TypeRepr>> args;
    args.push_back(makeStringType());
    args.push_back(makeI32Type());
    auto gen = std::make_unique<GenericTypeRepr>("Map", std::move(args));

    EXPECT_EQ(gen->getKind(), TypeRepr::Kind::Generic);
    EXPECT_EQ(gen->getBaseName(), "Map");
    EXPECT_EQ(gen->getTypeArgs().size(), 2u);
    EXPECT_EQ(gen->getTypeArgs()[0]->toString(), "string");
    EXPECT_EQ(gen->getTypeArgs()[1]->toString(), "i32");
    EXPECT_EQ(gen->toString(), "Map<string, i32>");
}

// ============================================================
// cloneTypeRepr
// ============================================================

TEST(TypeTest, ClonePrimitive) {
    auto orig = makeI32Type();
    auto clone = cloneTypeRepr(orig.get());

    EXPECT_NE(orig.get(), clone.get());
    EXPECT_EQ(clone->getKind(), TypeRepr::Kind::I32);
    EXPECT_EQ(clone->toString(), orig->toString());
    EXPECT_TRUE(clone->isInteger());
    EXPECT_TRUE(clone->isSignedInteger());
    EXPECT_EQ(clone->getBitWidth(), 32);
}

TEST(TypeTest, CloneNamedType) {
    auto orig = makeNamedType("MyStruct");
    auto clone = cloneTypeRepr(orig.get());

    EXPECT_NE(orig.get(), clone.get());
    EXPECT_EQ(clone->getKind(), TypeRepr::Kind::Named);
    EXPECT_EQ(clone->toString(), "MyStruct");
    auto *named = static_cast<NamedTypeRepr *>(clone.get());
    EXPECT_EQ(named->getName(), "MyStruct");
}

TEST(TypeTest, CloneOptionalType) {
    auto inner = makeI32Type();
    auto orig = std::make_unique<OptionalTypeRepr>(std::move(inner));
    auto clone = cloneTypeRepr(orig.get());

    EXPECT_NE(orig.get(), clone.get());
    EXPECT_EQ(clone->getKind(), TypeRepr::Kind::Optional);
    EXPECT_EQ(clone->toString(), "i32?");
    auto *opt = static_cast<OptionalTypeRepr *>(clone.get());
    EXPECT_EQ(opt->getInner()->toString(), "i32");
}

TEST(TypeTest, CloneArrayType) {
    auto elem = makeStringType();
    auto orig = std::make_unique<ArrayTypeRepr>(std::move(elem), 3);
    auto clone = cloneTypeRepr(orig.get());

    EXPECT_NE(orig.get(), clone.get());
    EXPECT_EQ(clone->getKind(), TypeRepr::Kind::Array);
    EXPECT_EQ(clone->toString(), "[string; 3]");
    auto *arr = static_cast<ArrayTypeRepr *>(clone.get());
    EXPECT_EQ(arr->getSize(), 3);
    EXPECT_FALSE(arr->isDynamic());
    EXPECT_EQ(arr->getElement()->toString(), "string");
}

TEST(TypeTest, CloneDynamicArrayType) {
    auto elem = makeF64Type();
    auto orig = std::make_unique<ArrayTypeRepr>(std::move(elem));
    auto clone = cloneTypeRepr(orig.get());

    EXPECT_NE(orig.get(), clone.get());
    EXPECT_EQ(clone->getKind(), TypeRepr::Kind::Array);
    EXPECT_EQ(clone->toString(), "[f64]");
    auto *arr = static_cast<ArrayTypeRepr *>(clone.get());
    EXPECT_TRUE(arr->isDynamic());
}

TEST(TypeTest, CloneResultType) {
    auto ok = makeI32Type();
    auto err = makeStringType();
    auto orig = std::make_unique<ResultTypeRepr>(std::move(ok), std::move(err));
    auto clone = cloneTypeRepr(orig.get());

    EXPECT_NE(orig.get(), clone.get());
    EXPECT_EQ(clone->getKind(), TypeRepr::Kind::Result);
    EXPECT_EQ(clone->toString(), "Result<i32, string>");
    auto *res = static_cast<ResultTypeRepr *>(clone.get());
    EXPECT_EQ(res->getOkType()->toString(), "i32");
    EXPECT_EQ(res->getErrType()->toString(), "string");
}

TEST(TypeTest, CloneFunctionType) {
    std::vector<std::unique_ptr<TypeRepr>> params;
    params.push_back(makeI32Type());
    params.push_back(makeBoolType());
    auto ret = makeStringType();
    auto orig = std::make_unique<FunctionTypeRepr>(std::move(params), std::move(ret));
    auto clone = cloneTypeRepr(orig.get());

    EXPECT_NE(orig.get(), clone.get());
    EXPECT_EQ(clone->getKind(), TypeRepr::Kind::Function);
    EXPECT_EQ(clone->toString(), "(i32, bool) -> string");
    auto *fn = static_cast<FunctionTypeRepr *>(clone.get());
    EXPECT_EQ(fn->getParams().size(), 2u);
    EXPECT_EQ(fn->getReturnType()->toString(), "string");
}

TEST(TypeTest, CloneTupleType) {
    std::vector<std::unique_ptr<TypeRepr>> elems;
    elems.push_back(makeI32Type());
    elems.push_back(makeF64Type());
    elems.push_back(makeStringType());
    auto orig = std::make_unique<TupleTypeRepr>(std::move(elems));
    auto clone = cloneTypeRepr(orig.get());

    EXPECT_NE(orig.get(), clone.get());
    EXPECT_EQ(clone->getKind(), TypeRepr::Kind::Tuple);
    EXPECT_EQ(clone->toString(), "(i32, f64, string)");
    auto *tup = static_cast<TupleTypeRepr *>(clone.get());
    EXPECT_EQ(tup->getArity(), 3u);
}

// ============================================================
// Type Properties Edge Cases
// ============================================================

TEST(TypeTest, VoidTypeProperties) {
    auto v = makeVoidType();
    EXPECT_TRUE(v->isVoid());
    EXPECT_TRUE(v->isPrimitive());
    EXPECT_FALSE(v->isNumeric());
    EXPECT_FALSE(v->isInteger());
    EXPECT_FALSE(v->isFloat());
    EXPECT_FALSE(v->isBool());
    EXPECT_FALSE(v->isReference());
    EXPECT_FALSE(v->isInferred());
    EXPECT_EQ(v->getBitWidth(), 0);
}

TEST(TypeTest, StringTypeProperties) {
    auto s = makeStringType();
    EXPECT_TRUE(s->isPrimitive());
    EXPECT_FALSE(s->isNumeric());
    EXPECT_FALSE(s->isInteger());
    EXPECT_FALSE(s->isFloat());
    EXPECT_FALSE(s->isBool());
    EXPECT_FALSE(s->isVoid());
    EXPECT_FALSE(s->isReference());
    EXPECT_FALSE(s->isInferred());
    EXPECT_EQ(s->getBitWidth(), 0);
    EXPECT_EQ(s->getKind(), TypeRepr::Kind::String);
}

TEST(TypeTest, NamedTypeProperties) {
    auto t = makeNamedType("Foo");
    EXPECT_FALSE(t->isPrimitive());
    EXPECT_FALSE(t->isNumeric());
    EXPECT_FALSE(t->isInteger());
    EXPECT_FALSE(t->isFloat());
    EXPECT_FALSE(t->isBool());
    EXPECT_FALSE(t->isVoid());
    EXPECT_FALSE(t->isReference());
    EXPECT_FALSE(t->isInferred());
    EXPECT_EQ(t->getBitWidth(), 0);
}

// ============================================================
// Complex Generic Types
// ============================================================

TEST(TypeTest, NestedGenericType) {
    // Vec<Vec<i32>>
    std::vector<std::unique_ptr<TypeRepr>> innerArgs;
    innerArgs.push_back(makeI32Type());
    auto innerGen = std::make_unique<GenericTypeRepr>("Vec", std::move(innerArgs));

    std::vector<std::unique_ptr<TypeRepr>> outerArgs;
    outerArgs.push_back(std::move(innerGen));
    auto outerGen = std::make_unique<GenericTypeRepr>("Vec", std::move(outerArgs));

    EXPECT_EQ(outerGen->getKind(), TypeRepr::Kind::Generic);
    EXPECT_EQ(outerGen->getBaseName(), "Vec");
    EXPECT_EQ(outerGen->getTypeArgs().size(), 1u);
    EXPECT_EQ(outerGen->getTypeArgs()[0]->getKind(), TypeRepr::Kind::Generic);
    EXPECT_EQ(outerGen->toString(), "Vec<Vec<i32>>");
}

TEST(TypeTest, GenericResultWithOptional) {
    // Result<i32?, string>
    auto inner = makeI32Type();
    auto optOk = std::make_unique<OptionalTypeRepr>(std::move(inner));
    auto err = makeStringType();
    auto res = std::make_unique<ResultTypeRepr>(std::move(optOk), std::move(err));

    EXPECT_EQ(res->getKind(), TypeRepr::Kind::Result);
    EXPECT_EQ(res->getOkType()->getKind(), TypeRepr::Kind::Optional);
    EXPECT_EQ(res->getErrType()->toString(), "string");
    EXPECT_EQ(res->toString(), "Result<i32?, string>");
}

// ============================================================
// Additional Edge Cases
// ============================================================

TEST(TypeTest, CloneNullReturnsNull) {
    auto result = cloneTypeRepr(nullptr);
    EXPECT_EQ(result, nullptr);
}

TEST(TypeTest, InferredTypeProperties) {
    auto inf = makeInferredType();
    EXPECT_TRUE(inf->isInferred());
    EXPECT_FALSE(inf->isPrimitive());
    EXPECT_FALSE(inf->isNumeric());
    EXPECT_FALSE(inf->isInteger());
    EXPECT_FALSE(inf->isFloat());
    EXPECT_FALSE(inf->isBool());
    EXPECT_FALSE(inf->isVoid());
    EXPECT_FALSE(inf->isReference());
    EXPECT_EQ(inf->getKind(), TypeRepr::Kind::Inferred);
}

TEST(TypeTest, BoolTypeNotNumeric) {
    auto b = makeBoolType();
    EXPECT_TRUE(b->isBool());
    EXPECT_FALSE(b->isNumeric());
    EXPECT_FALSE(b->isInteger());
    EXPECT_FALSE(b->isFloat());
    EXPECT_TRUE(b->isPrimitive());
    EXPECT_FALSE(b->isVoid());
}

TEST(TypeTest, MutableReferenceToOptional) {
    // ref mut i32?
    auto inner = makeI32Type();
    auto opt = std::make_unique<OptionalTypeRepr>(std::move(inner));
    auto ref = std::make_unique<ReferenceTypeRepr>(std::move(opt), true);

    EXPECT_TRUE(ref->isReference());
    EXPECT_TRUE(ref->isMutable());
    EXPECT_EQ(ref->getInner()->getKind(), TypeRepr::Kind::Optional);
    EXPECT_EQ(ref->toString(), "ref mut i32?");
}

TEST(TypeTest, FunctionTypeWithComplexParams) {
    // (ref i32, [string]) -> bool
    std::vector<std::unique_ptr<TypeRepr>> params;

    auto refInner = makeI32Type();
    params.push_back(std::make_unique<ReferenceTypeRepr>(std::move(refInner), false));

    auto arrElem = makeStringType();
    params.push_back(std::make_unique<ArrayTypeRepr>(std::move(arrElem)));

    auto ret = makeBoolType();
    auto fn = std::make_unique<FunctionTypeRepr>(std::move(params), std::move(ret));

    EXPECT_EQ(fn->getParams().size(), 2u);
    EXPECT_EQ(fn->getParams()[0]->toString(), "ref i32");
    EXPECT_EQ(fn->getParams()[1]->toString(), "[string]");
    EXPECT_EQ(fn->toString(), "(ref i32, [string]) -> bool");
}

TEST(TypeTest, OptionalOfOptional) {
    // i32??
    auto inner = makeI32Type();
    auto opt1 = std::make_unique<OptionalTypeRepr>(std::move(inner));
    auto opt2 = std::make_unique<OptionalTypeRepr>(std::move(opt1));

    EXPECT_EQ(opt2->getKind(), TypeRepr::Kind::Optional);
    EXPECT_EQ(opt2->getInner()->getKind(), TypeRepr::Kind::Optional);
    EXPECT_EQ(opt2->toString(), "i32??");
}

TEST(TypeTest, ArrayOfArrays) {
    // [[i32]; 3]
    auto innerElem = makeI32Type();
    auto innerArr = std::make_unique<ArrayTypeRepr>(std::move(innerElem));
    auto outerArr = std::make_unique<ArrayTypeRepr>(std::move(innerArr), 3);

    EXPECT_EQ(outerArr->getKind(), TypeRepr::Kind::Array);
    EXPECT_EQ(outerArr->getElement()->getKind(), TypeRepr::Kind::Array);
    EXPECT_EQ(outerArr->getSize(), 3);
    EXPECT_EQ(outerArr->toString(), "[[i32]; 3]");
}

TEST(TypeTest, GenericTypeBaseName) {
    std::vector<std::unique_ptr<TypeRepr>> args;
    args.push_back(makeBoolType());
    auto gen = std::make_unique<GenericTypeRepr>("Optional", std::move(args));

    EXPECT_EQ(gen->getBaseName(), "Optional");
    EXPECT_EQ(gen->getTypeArgs().size(), 1u);
    EXPECT_EQ(gen->getTypeArgs()[0]->toString(), "bool");
    EXPECT_EQ(gen->toString(), "Optional<bool>");
}
