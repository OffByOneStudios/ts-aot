#include "AstSerializer.h"
#include <stdexcept>

using json = nlohmann::json;

namespace ts::parser {

// ============================================================================
// Helpers
// ============================================================================

void AstSerializer::addLocation(json& j, const ast::Node& node) {
    j["line"] = node.line;
    j["column"] = node.column;
    if (!node.sourceFile.empty()) {
        j["sourceFile"] = node.sourceFile;
    }
}

std::string AstSerializer::accessModifierToString(ts::AccessModifier mod) {
    switch (mod) {
        case ts::AccessModifier::Private: return "private";
        case ts::AccessModifier::Protected: return "protected";
        default: return "public";
    }
}

json AstSerializer::serializeNodePtr(const ast::NodePtr& ptr) {
    if (!ptr) return nullptr;
    return serializeNode(*ptr);
}

json AstSerializer::serializeExprPtr(const ast::ExprPtr& ptr) {
    if (!ptr) return nullptr;
    return serializeExpression(*ptr);
}

json AstSerializer::serializeStmtPtr(const ast::StmtPtr& ptr) {
    if (!ptr) return nullptr;
    return serializeStatement(*ptr);
}

json AstSerializer::serializeDecorators(const std::vector<ast::Decorator>& decorators) {
    json arr = json::array();
    for (const auto& dec : decorators) {
        json d;
        d["name"] = dec.name;
        if (dec.expression) {
            d["expression"] = serializeExpression(*dec.expression);
        }
        arr.push_back(d);
    }
    return arr;
}

// ============================================================================
// Program (top-level)
// ============================================================================

json AstSerializer::serialize(const ast::Program& program) {
    json j;
    j["kind"] = "Program";
    addLocation(j, program);

    json body = json::array();
    for (const auto& stmt : program.body) {
        body.push_back(serializeStatement(*stmt));
    }
    j["body"] = body;

    if (!program.tripleSlashReferences.empty()) {
        json refs = json::array();
        for (const auto& ref : program.tripleSlashReferences) {
            json r;
            r["type"] = ref.type;
            r["path"] = ref.path;
            refs.push_back(r);
        }
        j["tripleSlashReferences"] = refs;
    }

    return j;
}

// ============================================================================
// Node dispatch (generic)
// ============================================================================

json AstSerializer::serializeNode(const ast::Node& node) {
    std::string kind = node.getKind();

    // Expression types
    if (kind == "Identifier") return serializeIdentifier(static_cast<const ast::Identifier&>(node));
    if (kind == "BinaryExpression") return serializeBinaryExpr(static_cast<const ast::BinaryExpression&>(node));
    if (kind == "AssignmentExpression") return serializeAssignmentExpr(static_cast<const ast::AssignmentExpression&>(node));
    if (kind == "CallExpression") return serializeCallExpr(static_cast<const ast::CallExpression&>(node));
    if (kind == "NewExpression") return serializeNewExpr(static_cast<const ast::NewExpression&>(node));
    if (kind == "PropertyAccessExpression") return serializePropertyAccess(static_cast<const ast::PropertyAccessExpression&>(node));
    if (kind == "ElementAccessExpression") return serializeElementAccess(static_cast<const ast::ElementAccessExpression&>(node));
    if (kind == "ConditionalExpression") return serializeConditionalExpr(static_cast<const ast::ConditionalExpression&>(node));
    if (kind == "ArrowFunction") return serializeArrowFunction(static_cast<const ast::ArrowFunction&>(node));
    if (kind == "FunctionExpression") return serializeFunctionExpr(static_cast<const ast::FunctionExpression&>(node));
    if (kind == "ClassExpression") return serializeClassExpr(static_cast<const ast::ClassExpression&>(node));
    if (kind == "TemplateExpression") return serializeTemplateExpr(static_cast<const ast::TemplateExpression&>(node));
    if (kind == "TaggedTemplateExpression") return serializeTaggedTemplate(static_cast<const ast::TaggedTemplateExpression&>(node));
    if (kind == "ObjectLiteralExpression") return serializeObjectLiteral(static_cast<const ast::ObjectLiteralExpression&>(node));
    if (kind == "ArrayLiteralExpression") return serializeArrayLiteral(static_cast<const ast::ArrayLiteralExpression&>(node));

    // Statement types
    if (kind == "FunctionDeclaration") return serializeFunctionDecl(static_cast<const ast::FunctionDeclaration&>(node));
    if (kind == "ClassDeclaration") return serializeClassDecl(static_cast<const ast::ClassDeclaration&>(node));
    if (kind == "VariableDeclaration") return serializeVariableDecl(static_cast<const ast::VariableDeclaration&>(node));
    if (kind == "BlockStatement") return serializeBlockStmt(static_cast<const ast::BlockStatement&>(node));
    if (kind == "ExpressionStatement") return serializeExprStmt(static_cast<const ast::ExpressionStatement&>(node));
    if (kind == "ReturnStatement") return serializeReturnStmt(static_cast<const ast::ReturnStatement&>(node));
    if (kind == "IfStatement") return serializeIfStmt(static_cast<const ast::IfStatement&>(node));
    if (kind == "WhileStatement") return serializeWhileStmt(static_cast<const ast::WhileStatement&>(node));
    if (kind == "ForStatement") return serializeForStmt(static_cast<const ast::ForStatement&>(node));
    if (kind == "ForOfStatement") return serializeForOfStmt(static_cast<const ast::ForOfStatement&>(node));
    if (kind == "ForInStatement") return serializeForInStmt(static_cast<const ast::ForInStatement&>(node));
    if (kind == "SwitchStatement") return serializeSwitchStmt(static_cast<const ast::SwitchStatement&>(node));
    if (kind == "TryStatement") return serializeTryStmt(static_cast<const ast::TryStatement&>(node));
    if (kind == "ThrowStatement") return serializeThrowStmt(static_cast<const ast::ThrowStatement&>(node));
    if (kind == "ImportDeclaration") return serializeImportDecl(static_cast<const ast::ImportDeclaration&>(node));
    if (kind == "ExportDeclaration") return serializeExportDecl(static_cast<const ast::ExportDeclaration&>(node));
    if (kind == "ExportAssignment") return serializeExportAssignment(static_cast<const ast::ExportAssignment&>(node));
    if (kind == "BreakStatement") return serializeBreakStmt(static_cast<const ast::BreakStatement&>(node));
    if (kind == "ContinueStatement") return serializeContinueStmt(static_cast<const ast::ContinueStatement&>(node));
    if (kind == "LabeledStatement") return serializeLabeledStmt(static_cast<const ast::LabeledStatement&>(node));
    if (kind == "InterfaceDeclaration") return serializeInterfaceDecl(static_cast<const ast::InterfaceDeclaration&>(node));
    if (kind == "TypeAliasDeclaration") return serializeTypeAliasDecl(static_cast<const ast::TypeAliasDeclaration&>(node));
    if (kind == "EnumDeclaration") return serializeEnumDecl(static_cast<const ast::EnumDeclaration&>(node));

    // Class members
    if (kind == "MethodDefinition") return serializeMethodDef(static_cast<const ast::MethodDefinition&>(node));
    if (kind == "PropertyDefinition") return serializePropertyDef(static_cast<const ast::PropertyDefinition&>(node));
    if (kind == "StaticBlock") {
        const auto& sb = static_cast<const ast::StaticBlock&>(node);
        json j;
        j["kind"] = "StaticBlock";
        addLocation(j, sb);
        json body = json::array();
        for (const auto& stmt : sb.body) body.push_back(serializeStatement(*stmt));
        j["body"] = body;
        return j;
    }
    if (kind == "IndexSignature") {
        const auto& idx = static_cast<const ast::IndexSignature&>(node);
        json j;
        j["kind"] = "IndexSignature";
        addLocation(j, idx);
        j["keyType"] = idx.keyType;
        j["valueType"] = idx.valueType;
        if (idx.isReadonly) j["isReadonly"] = true;
        return j;
    }

    // Binding patterns
    if (kind == "ObjectBindingPattern") {
        const auto& obp = static_cast<const ast::ObjectBindingPattern&>(node);
        json j;
        j["kind"] = "ObjectBindingPattern";
        addLocation(j, obp);
        json elements = json::array();
        for (const auto& el : obp.elements) elements.push_back(serializeNodePtr(el));
        j["elements"] = elements;
        return j;
    }
    if (kind == "ArrayBindingPattern") {
        const auto& abp = static_cast<const ast::ArrayBindingPattern&>(node);
        json j;
        j["kind"] = "ArrayBindingPattern";
        addLocation(j, abp);
        json elements = json::array();
        for (const auto& el : abp.elements) elements.push_back(serializeNodePtr(el));
        j["elements"] = elements;
        return j;
    }
    if (kind == "BindingElement") {
        const auto& be = static_cast<const ast::BindingElement&>(node);
        json j;
        j["kind"] = "BindingElement";
        addLocation(j, be);
        j["name"] = serializeNodePtr(be.name);
        if (!be.propertyName.empty()) j["propertyName"] = be.propertyName;
        if (be.initializer) j["initializer"] = serializeExprPtr(be.initializer);
        if (be.isSpread) j["isSpread"] = true;
        return j;
    }
    if (kind == "ComputedPropertyName") {
        const auto& cpn = static_cast<const ast::ComputedPropertyName&>(node);
        json j;
        j["kind"] = "ComputedPropertyName";
        addLocation(j, cpn);
        j["expression"] = serializeExprPtr(cpn.expression);
        return j;
    }
    if (kind == "OmittedExpression") {
        json j;
        j["kind"] = "OmittedExpression";
        addLocation(j, node);
        return j;
    }

    // Property assignments (in object literals)
    if (kind == "PropertyAssignment") {
        const auto& pa = static_cast<const ast::PropertyAssignment&>(node);
        json j;
        j["kind"] = "PropertyAssignment";
        addLocation(j, pa);
        j["name"] = pa.name;
        if (pa.nameNode) j["nameNode"] = serializeNodePtr(pa.nameNode);
        j["initializer"] = serializeExprPtr(pa.initializer);
        return j;
    }
    if (kind == "ShorthandPropertyAssignment") {
        const auto& spa = static_cast<const ast::ShorthandPropertyAssignment&>(node);
        json j;
        j["kind"] = "ShorthandPropertyAssignment";
        addLocation(j, spa);
        j["name"] = spa.name;
        if (spa.nameNode) j["nameNode"] = serializeNodePtr(spa.nameNode);
        return j;
    }

    // Spread
    if (kind == "SpreadElement") {
        const auto& se = static_cast<const ast::SpreadElement&>(node);
        json j;
        j["kind"] = "SpreadElement";
        addLocation(j, se);
        j["expression"] = serializeExprPtr(se.expression);
        return j;
    }

    // Remaining expression types
    if (kind == "PrefixUnaryExpression") {
        const auto& pue = static_cast<const ast::PrefixUnaryExpression&>(node);
        json j;
        j["kind"] = "PrefixUnaryExpression";
        addLocation(j, pue);
        j["operator"] = pue.op;
        j["operand"] = serializeExprPtr(pue.operand);
        return j;
    }
    if (kind == "PostfixUnaryExpression") {
        const auto& pue = static_cast<const ast::PostfixUnaryExpression&>(node);
        json j;
        j["kind"] = "PostfixUnaryExpression";
        addLocation(j, pue);
        j["operator"] = pue.op;
        j["operand"] = serializeExprPtr(pue.operand);
        return j;
    }
    if (kind == "DeleteExpression") {
        const auto& de = static_cast<const ast::DeleteExpression&>(node);
        json j;
        j["kind"] = "DeleteExpression";
        addLocation(j, de);
        j["expression"] = serializeExprPtr(de.expression);
        return j;
    }
    if (kind == "AwaitExpression") {
        const auto& ae = static_cast<const ast::AwaitExpression&>(node);
        json j;
        j["kind"] = "AwaitExpression";
        addLocation(j, ae);
        j["expression"] = serializeExprPtr(ae.expression);
        return j;
    }
    if (kind == "YieldExpression") {
        const auto& ye = static_cast<const ast::YieldExpression&>(node);
        json j;
        j["kind"] = "YieldExpression";
        addLocation(j, ye);
        if (ye.expression) j["expression"] = serializeExprPtr(ye.expression);
        if (ye.isAsterisk) j["isAsterisk"] = true;
        return j;
    }
    if (kind == "AsExpression") {
        const auto& ae = static_cast<const ast::AsExpression&>(node);
        json j;
        j["kind"] = "AsExpression";
        addLocation(j, ae);
        j["expression"] = serializeExprPtr(ae.expression);
        j["type"] = ae.type;
        return j;
    }
    if (kind == "NonNullExpression") {
        const auto& nne = static_cast<const ast::NonNullExpression&>(node);
        json j;
        j["kind"] = "NonNullExpression";
        addLocation(j, nne);
        j["expression"] = serializeExprPtr(nne.expression);
        return j;
    }
    if (kind == "ParenthesizedExpression") {
        const auto& pe = static_cast<const ast::ParenthesizedExpression&>(node);
        json j;
        j["kind"] = "ParenthesizedExpression";
        addLocation(j, pe);
        j["expression"] = serializeExprPtr(pe.expression);
        return j;
    }
    if (kind == "DynamicImport") {
        const auto& di = static_cast<const ast::DynamicImport&>(node);
        json j;
        j["kind"] = "DynamicImport";
        addLocation(j, di);
        if (di.moduleSpecifier) j["moduleSpecifier"] = serializeExprPtr(di.moduleSpecifier);
        return j;
    }

    // Literals
    if (kind == "StringLiteral") {
        const auto& sl = static_cast<const ast::StringLiteral&>(node);
        json j;
        j["kind"] = "StringLiteral";
        addLocation(j, sl);
        j["value"] = sl.value;
        return j;
    }
    if (kind == "NumericLiteral") {
        const auto& nl = static_cast<const ast::NumericLiteral&>(node);
        json j;
        j["kind"] = "NumericLiteral";
        addLocation(j, nl);
        j["value"] = nl.value;
        return j;
    }
    if (kind == "BigIntLiteral") {
        const auto& bl = static_cast<const ast::BigIntLiteral&>(node);
        json j;
        j["kind"] = "BigIntLiteral";
        addLocation(j, bl);
        j["value"] = bl.value;
        return j;
    }
    if (kind == "BooleanLiteral") {
        const auto& bl = static_cast<const ast::BooleanLiteral&>(node);
        json j;
        j["kind"] = "BooleanLiteral";
        addLocation(j, bl);
        j["value"] = bl.value;
        return j;
    }
    if (kind == "NullLiteral") {
        json j;
        j["kind"] = "NullLiteral";
        addLocation(j, node);
        return j;
    }
    if (kind == "UndefinedLiteral") {
        json j;
        j["kind"] = "UndefinedLiteral";
        addLocation(j, node);
        return j;
    }
    if (kind == "RegularExpressionLiteral") {
        const auto& rel = static_cast<const ast::RegularExpressionLiteral&>(node);
        json j;
        j["kind"] = "RegularExpressionLiteral";
        addLocation(j, rel);
        j["text"] = rel.text;
        return j;
    }
    if (kind == "SuperExpression") {
        json j;
        j["kind"] = "SuperExpression";
        addLocation(j, node);
        return j;
    }

    // JSX
    if (kind == "JsxElement") {
        const auto& jsx = static_cast<const ast::JsxElement&>(node);
        json j;
        j["kind"] = "JsxElement";
        addLocation(j, jsx);
        // Produce openingElement structure matching dump_ast.js
        json opening;
        opening["tagName"] = jsx.tagName;
        json attrs = json::array();
        for (const auto& attr : jsx.attributes) attrs.push_back(serializeNodePtr(attr));
        opening["attributes"] = attrs;
        j["openingElement"] = opening;
        json children = json::array();
        for (const auto& child : jsx.children) children.push_back(serializeExprPtr(child));
        j["children"] = children;
        return j;
    }
    if (kind == "JsxSelfClosingElement") {
        const auto& jsx = static_cast<const ast::JsxSelfClosingElement&>(node);
        json j;
        j["kind"] = "JsxSelfClosingElement";
        addLocation(j, jsx);
        j["tagName"] = jsx.tagName;
        json attrs = json::array();
        for (const auto& attr : jsx.attributes) attrs.push_back(serializeNodePtr(attr));
        j["attributes"] = attrs;
        return j;
    }
    if (kind == "JsxFragment") {
        const auto& jsx = static_cast<const ast::JsxFragment&>(node);
        json j;
        j["kind"] = "JsxFragment";
        addLocation(j, jsx);
        json children = json::array();
        for (const auto& child : jsx.children) children.push_back(serializeExprPtr(child));
        j["children"] = children;
        return j;
    }
    if (kind == "JsxExpression") {
        const auto& jsx = static_cast<const ast::JsxExpression&>(node);
        json j;
        j["kind"] = "JsxExpression";
        addLocation(j, jsx);
        if (jsx.expression) j["expression"] = serializeExprPtr(jsx.expression);
        return j;
    }
    if (kind == "JsxText") {
        const auto& jsx = static_cast<const ast::JsxText&>(node);
        json j;
        j["kind"] = "JsxText";
        addLocation(j, jsx);
        j["text"] = jsx.text;
        return j;
    }
    if (kind == "JsxAttribute") {
        const auto& attr = static_cast<const ast::JsxAttribute&>(node);
        json j;
        j["kind"] = "JsxAttribute";
        addLocation(j, attr);
        j["name"] = attr.name;
        if (attr.initializer) j["initializer"] = serializeExprPtr(attr.initializer);
        return j;
    }
    if (kind == "JsxSpreadAttribute") {
        const auto& attr = static_cast<const ast::JsxSpreadAttribute&>(node);
        json j;
        j["kind"] = "JsxSpreadAttribute";
        addLocation(j, attr);
        if (attr.expression) j["expression"] = serializeExprPtr(attr.expression);
        return j;
    }

    // Fallback - serialize just the kind
    json j;
    j["kind"] = kind;
    addLocation(j, node);
    return j;
}

// ============================================================================
// Statement dispatch
// ============================================================================

json AstSerializer::serializeStatement(const ast::Statement& stmt) {
    return serializeNode(stmt);
}

// ============================================================================
// Expression dispatch
// ============================================================================

json AstSerializer::serializeExpression(const ast::Expression& expr) {
    return serializeNode(expr);
}

// ============================================================================
// Statement serializers
// ============================================================================

json AstSerializer::serializeFunctionDecl(const ast::FunctionDeclaration& fn) {
    json j;
    j["kind"] = "FunctionDeclaration";
    addLocation(j, fn);
    j["name"] = fn.name;
    if (fn.isExported) j["isExported"] = true;
    if (fn.isDefaultExport) j["isDefaultExport"] = true;
    if (fn.isAsync) j["isAsync"] = true;
    if (fn.isGenerator) j["isGenerator"] = true;

    json params = json::array();
    for (const auto& p : fn.parameters) params.push_back(serializeParameter(*p));
    j["parameters"] = params;

    if (!fn.typeParameters.empty()) {
        json tps = json::array();
        for (const auto& tp : fn.typeParameters) tps.push_back(serializeTypeParameter(*tp));
        j["typeParameters"] = tps;
    }

    if (!fn.returnType.empty()) j["returnType"] = fn.returnType;

    if (!fn.decorators.empty()) j["decorators"] = serializeDecorators(fn.decorators);

    json body = json::array();
    for (const auto& stmt : fn.body) body.push_back(serializeStatement(*stmt));
    j["body"] = body;

    return j;
}

json AstSerializer::serializeClassDecl(const ast::ClassDeclaration& cls) {
    json j;
    j["kind"] = "ClassDeclaration";
    addLocation(j, cls);
    j["name"] = cls.name;
    if (cls.isExported) j["isExported"] = true;
    if (cls.isDefaultExport) j["isDefaultExport"] = true;
    if (!cls.baseClass.empty()) j["baseClass"] = cls.baseClass;
    if (cls.isAbstract) j["isAbstract"] = true;
    if (cls.isStruct) j["isStruct"] = true;

    if (!cls.implementsInterfaces.empty()) {
        json ifaces = json::array();
        for (const auto& i : cls.implementsInterfaces) ifaces.push_back(i);
        j["implementsInterfaces"] = ifaces;
    }

    if (!cls.typeParameters.empty()) {
        json tps = json::array();
        for (const auto& tp : cls.typeParameters) tps.push_back(serializeTypeParameter(*tp));
        j["typeParameters"] = tps;
    }

    json members = json::array();
    for (const auto& m : cls.members) members.push_back(serializeNodePtr(m));
    j["members"] = members;

    if (!cls.decorators.empty()) j["decorators"] = serializeDecorators(cls.decorators);

    return j;
}

json AstSerializer::serializeVariableDecl(const ast::VariableDeclaration& decl) {
    json j;
    j["kind"] = "VariableDeclaration";
    addLocation(j, decl);
    j["name"] = serializeNodePtr(decl.name);
    if (decl.isExported) j["isExported"] = true;
    if (!decl.type.empty()) j["type"] = decl.type;
    if (decl.initializer) j["initializer"] = serializeExprPtr(decl.initializer);
    return j;
}

json AstSerializer::serializeIfStmt(const ast::IfStatement& stmt) {
    json j;
    j["kind"] = "IfStatement";
    addLocation(j, stmt);
    j["condition"] = serializeExprPtr(stmt.condition);
    j["thenStatement"] = serializeStmtPtr(stmt.thenStatement);
    if (stmt.elseStatement) j["elseStatement"] = serializeStmtPtr(stmt.elseStatement);
    return j;
}

json AstSerializer::serializeWhileStmt(const ast::WhileStatement& stmt) {
    json j;
    j["kind"] = "WhileStatement";
    addLocation(j, stmt);
    j["condition"] = serializeExprPtr(stmt.condition);
    // dump_ast.js uses "statement" for the body
    j["statement"] = serializeStmtPtr(stmt.body);
    return j;
}

json AstSerializer::serializeForStmt(const ast::ForStatement& stmt) {
    json j;
    j["kind"] = "ForStatement";
    addLocation(j, stmt);
    if (stmt.initializer) j["initializer"] = serializeStmtPtr(stmt.initializer);
    if (stmt.condition) j["condition"] = serializeExprPtr(stmt.condition);
    if (stmt.incrementor) j["incrementor"] = serializeExprPtr(stmt.incrementor);
    j["body"] = serializeStmtPtr(stmt.body);
    return j;
}

json AstSerializer::serializeForOfStmt(const ast::ForOfStatement& stmt) {
    json j;
    j["kind"] = "ForOfStatement";
    addLocation(j, stmt);
    if (stmt.initializer) j["initializer"] = serializeStmtPtr(stmt.initializer);
    j["expression"] = serializeExprPtr(stmt.expression);
    j["body"] = serializeStmtPtr(stmt.body);
    if (stmt.isAwait) j["isAwait"] = true;
    return j;
}

json AstSerializer::serializeForInStmt(const ast::ForInStatement& stmt) {
    json j;
    j["kind"] = "ForInStatement";
    addLocation(j, stmt);
    if (stmt.initializer) j["initializer"] = serializeStmtPtr(stmt.initializer);
    j["expression"] = serializeExprPtr(stmt.expression);
    j["body"] = serializeStmtPtr(stmt.body);
    return j;
}

json AstSerializer::serializeSwitchStmt(const ast::SwitchStatement& stmt) {
    json j;
    j["kind"] = "SwitchStatement";
    addLocation(j, stmt);
    j["expression"] = serializeExprPtr(stmt.expression);

    json clauses = json::array();
    for (const auto& clause : stmt.clauses) {
        if (auto* cc = dynamic_cast<const ast::CaseClause*>(clause.get())) {
            json c;
            c["kind"] = "CaseClause";
            c["expression"] = serializeExprPtr(cc->expression);
            json stmts = json::array();
            for (const auto& s : cc->statements) stmts.push_back(serializeStatement(*s));
            c["statements"] = stmts;
            clauses.push_back(c);
        } else if (auto* dc = dynamic_cast<const ast::DefaultClause*>(clause.get())) {
            json c;
            c["kind"] = "DefaultClause";
            json stmts = json::array();
            for (const auto& s : dc->statements) stmts.push_back(serializeStatement(*s));
            c["statements"] = stmts;
            clauses.push_back(c);
        }
    }
    j["clauses"] = clauses;
    return j;
}

json AstSerializer::serializeTryStmt(const ast::TryStatement& stmt) {
    json j;
    j["kind"] = "TryStatement";
    addLocation(j, stmt);

    json tryBlock = json::array();
    for (const auto& s : stmt.tryBlock) tryBlock.push_back(serializeStatement(*s));
    j["tryBlock"] = tryBlock;

    if (stmt.catchClause) {
        json cc;
        if (stmt.catchClause->variable) {
            cc["variable"] = serializeNodePtr(stmt.catchClause->variable);
        }
        json block = json::array();
        for (const auto& s : stmt.catchClause->block) block.push_back(serializeStatement(*s));
        cc["block"] = block;
        j["catchClause"] = cc;
    }

    if (!stmt.finallyBlock.empty()) {
        json fb = json::array();
        for (const auto& s : stmt.finallyBlock) fb.push_back(serializeStatement(*s));
        j["finallyBlock"] = fb;
    }

    return j;
}

json AstSerializer::serializeReturnStmt(const ast::ReturnStatement& stmt) {
    json j;
    j["kind"] = "ReturnStatement";
    addLocation(j, stmt);
    if (stmt.expression) j["expression"] = serializeExprPtr(stmt.expression);
    return j;
}

json AstSerializer::serializeThrowStmt(const ast::ThrowStatement& stmt) {
    json j;
    j["kind"] = "ThrowStatement";
    addLocation(j, stmt);
    j["expression"] = serializeExprPtr(stmt.expression);
    return j;
}

json AstSerializer::serializeBlockStmt(const ast::BlockStatement& stmt) {
    json j;
    j["kind"] = "BlockStatement";
    addLocation(j, stmt);
    json body = json::array();
    for (const auto& s : stmt.statements) body.push_back(serializeStatement(*s));
    j["body"] = body;
    return j;
}

json AstSerializer::serializeExprStmt(const ast::ExpressionStatement& stmt) {
    json j;
    j["kind"] = "ExpressionStatement";
    addLocation(j, stmt);
    j["expression"] = serializeExprPtr(stmt.expression);
    return j;
}

json AstSerializer::serializeImportDecl(const ast::ImportDeclaration& decl) {
    json j;
    j["kind"] = "ImportDeclaration";
    addLocation(j, decl);
    j["moduleSpecifier"] = decl.moduleSpecifier;

    // Reconstruct the importClause structure matching dump_ast.js
    bool hasClause = !decl.defaultImport.empty() ||
                     !decl.namedImports.empty() ||
                     !decl.namespaceImport.empty();
    if (hasClause) {
        json clause;
        if (!decl.defaultImport.empty()) {
            clause["name"] = decl.defaultImport;
        }

        if (!decl.namespaceImport.empty()) {
            json bindings;
            bindings["kind"] = "NamespaceImport";
            bindings["name"] = decl.namespaceImport;
            clause["namedBindings"] = bindings;
        } else if (!decl.namedImports.empty()) {
            json bindings;
            bindings["kind"] = "NamedImports";
            json elements = json::array();
            for (const auto& spec : decl.namedImports) {
                json el;
                el["name"] = spec.name;
                if (!spec.propertyName.empty()) el["propertyName"] = spec.propertyName;
                elements.push_back(el);
            }
            bindings["elements"] = elements;
            clause["namedBindings"] = bindings;
        }

        j["importClause"] = clause;
    }

    return j;
}

json AstSerializer::serializeExportDecl(const ast::ExportDeclaration& decl) {
    json j;
    j["kind"] = "ExportDeclaration";
    addLocation(j, decl);
    if (!decl.moduleSpecifier.empty()) j["moduleSpecifier"] = decl.moduleSpecifier;
    if (decl.isStarExport) j["isStarExport"] = true;
    if (!decl.namespaceExport.empty()) j["namespaceExport"] = decl.namespaceExport;

    if (!decl.namedExports.empty()) {
        json exports = json::array();
        for (const auto& spec : decl.namedExports) {
            json el;
            el["name"] = spec.name;
            if (!spec.propertyName.empty()) el["propertyName"] = spec.propertyName;
            exports.push_back(el);
        }
        j["namedExports"] = exports;
    }

    return j;
}

json AstSerializer::serializeExportAssignment(const ast::ExportAssignment& stmt) {
    json j;
    j["kind"] = "ExportAssignment";
    addLocation(j, stmt);
    j["expression"] = serializeExprPtr(stmt.expression);
    if (stmt.isExportEquals) j["isExportEquals"] = true;
    return j;
}

json AstSerializer::serializeLabeledStmt(const ast::LabeledStatement& stmt) {
    json j;
    j["kind"] = "LabeledStatement";
    addLocation(j, stmt);
    j["label"] = stmt.label;
    if (stmt.statement) j["statement"] = serializeStmtPtr(stmt.statement);
    return j;
}

json AstSerializer::serializeBreakStmt(const ast::BreakStatement& stmt) {
    json j;
    j["kind"] = "BreakStatement";
    addLocation(j, stmt);
    if (!stmt.label.empty()) j["label"] = stmt.label;
    return j;
}

json AstSerializer::serializeContinueStmt(const ast::ContinueStatement& stmt) {
    json j;
    j["kind"] = "ContinueStatement";
    addLocation(j, stmt);
    if (!stmt.label.empty()) j["label"] = stmt.label;
    return j;
}

json AstSerializer::serializeInterfaceDecl(const ast::InterfaceDeclaration& decl) {
    json j;
    j["kind"] = "InterfaceDeclaration";
    addLocation(j, decl);
    j["name"] = decl.name;
    if (decl.isExported) j["isExported"] = true;
    if (decl.isDefaultExport) j["isDefaultExport"] = true;

    if (!decl.baseInterfaces.empty()) {
        json bases = json::array();
        for (const auto& b : decl.baseInterfaces) bases.push_back(b);
        j["baseInterfaces"] = bases;
    }

    if (!decl.typeParameters.empty()) {
        json tps = json::array();
        for (const auto& tp : decl.typeParameters) tps.push_back(serializeTypeParameter(*tp));
        j["typeParameters"] = tps;
    }

    // Members includes regular members, call signatures, and construct signatures
    json members = json::array();
    for (const auto& m : decl.members) members.push_back(serializeNodePtr(m));
    // Serialize call signatures as members
    for (const auto& cs : decl.callSignatures) {
        json csj;
        csj["kind"] = "CallSignature";
        json params = json::array();
        for (const auto& p : cs->parameters) params.push_back(serializeParameter(*p));
        csj["parameters"] = params;
        if (!cs->typeParameters.empty()) {
            json tps = json::array();
            for (const auto& tp : cs->typeParameters) tps.push_back(serializeTypeParameter(*tp));
            csj["typeParameters"] = tps;
        }
        if (!cs->returnType.empty()) csj["returnType"] = cs->returnType;
        members.push_back(csj);
    }
    for (const auto& cs : decl.constructSignatures) {
        json csj;
        csj["kind"] = "ConstructSignature";
        json params = json::array();
        for (const auto& p : cs->parameters) params.push_back(serializeParameter(*p));
        csj["parameters"] = params;
        if (!cs->typeParameters.empty()) {
            json tps = json::array();
            for (const auto& tp : cs->typeParameters) tps.push_back(serializeTypeParameter(*tp));
            csj["typeParameters"] = tps;
        }
        if (!cs->returnType.empty()) csj["returnType"] = cs->returnType;
        members.push_back(csj);
    }
    j["members"] = members;

    return j;
}

json AstSerializer::serializeTypeAliasDecl(const ast::TypeAliasDeclaration& decl) {
    json j;
    j["kind"] = "TypeAliasDeclaration";
    addLocation(j, decl);
    j["name"] = decl.name;
    j["type"] = decl.type;
    if (decl.isExported) j["isExported"] = true;
    if (!decl.typeParameters.empty()) {
        json tps = json::array();
        for (const auto& tp : decl.typeParameters) {
            json tpj;
            tpj["name"] = tp.name;
            if (!tp.constraint.empty()) tpj["constraint"] = tp.constraint;
            if (!tp.defaultType.empty()) tpj["default"] = tp.defaultType;
            tps.push_back(tpj);
        }
        j["typeParameters"] = tps;
    }
    return j;
}

json AstSerializer::serializeEnumDecl(const ast::EnumDeclaration& decl) {
    json j;
    j["kind"] = "EnumDeclaration";
    addLocation(j, decl);
    j["name"] = decl.name;
    if (decl.isExported) j["isExported"] = true;
    if (decl.isDeclare) j["isDeclare"] = true;
    json members = json::array();
    for (const auto& m : decl.members) {
        json mj;
        mj["name"] = m.name;
        if (m.initializer) mj["initializer"] = serializeExpression(*m.initializer);
        members.push_back(mj);
    }
    j["members"] = members;
    return j;
}

// ============================================================================
// Expression serializers
// ============================================================================

json AstSerializer::serializeIdentifier(const ast::Identifier& id) {
    json j;
    j["kind"] = "Identifier";
    addLocation(j, id);
    j["name"] = id.name;
    if (id.isPrivate) j["isPrivate"] = true;
    return j;
}

json AstSerializer::serializeCallExpr(const ast::CallExpression& call) {
    json j;
    j["kind"] = "CallExpression";
    addLocation(j, call);
    j["callee"] = serializeExprPtr(call.callee);

    json args = json::array();
    for (const auto& arg : call.arguments) args.push_back(serializeExprPtr(arg));
    j["arguments"] = args;

    if (!call.typeArguments.empty()) {
        json ta = json::array();
        for (const auto& t : call.typeArguments) ta.push_back(t);
        j["typeArguments"] = ta;
    }

    if (call.isOptional) j["isOptional"] = true;

    return j;
}

json AstSerializer::serializePropertyAccess(const ast::PropertyAccessExpression& pa) {
    json j;
    j["kind"] = "PropertyAccessExpression";
    addLocation(j, pa);
    j["expression"] = serializeExprPtr(pa.expression);
    j["name"] = pa.name;
    if (pa.isOptional) j["isOptional"] = true;
    return j;
}

json AstSerializer::serializeElementAccess(const ast::ElementAccessExpression& ea) {
    json j;
    j["kind"] = "ElementAccessExpression";
    addLocation(j, ea);
    j["expression"] = serializeExprPtr(ea.expression);
    j["argumentExpression"] = serializeExprPtr(ea.argumentExpression);
    if (ea.isOptional) j["isOptional"] = true;
    return j;
}

json AstSerializer::serializeBinaryExpr(const ast::BinaryExpression& bin) {
    json j;
    j["kind"] = "BinaryExpression";
    addLocation(j, bin);
    j["operator"] = bin.op;
    j["left"] = serializeExprPtr(bin.left);
    j["right"] = serializeExprPtr(bin.right);
    return j;
}

json AstSerializer::serializeAssignmentExpr(const ast::AssignmentExpression& assign) {
    json j;
    j["kind"] = "AssignmentExpression";
    addLocation(j, assign);
    j["left"] = serializeExprPtr(assign.left);
    j["right"] = serializeExprPtr(assign.right);
    return j;
}

json AstSerializer::serializeConditionalExpr(const ast::ConditionalExpression& cond) {
    json j;
    j["kind"] = "ConditionalExpression";
    addLocation(j, cond);
    j["condition"] = serializeExprPtr(cond.condition);
    j["whenTrue"] = serializeExprPtr(cond.whenTrue);
    j["whenFalse"] = serializeExprPtr(cond.whenFalse);
    return j;
}

json AstSerializer::serializeArrowFunction(const ast::ArrowFunction& fn) {
    json j;
    j["kind"] = "ArrowFunction";
    addLocation(j, fn);
    if (fn.isAsync) j["isAsync"] = true;

    json params = json::array();
    for (const auto& p : fn.parameters) params.push_back(serializeParameter(*p));
    j["parameters"] = params;

    j["body"] = serializeNodePtr(fn.body);

    return j;
}

json AstSerializer::serializeFunctionExpr(const ast::FunctionExpression& fn) {
    json j;
    j["kind"] = "FunctionExpression";
    addLocation(j, fn);
    if (!fn.name.empty()) j["name"] = fn.name;
    if (fn.isAsync) j["isAsync"] = true;
    if (fn.isGenerator) j["isGenerator"] = true;

    json params = json::array();
    for (const auto& p : fn.parameters) params.push_back(serializeParameter(*p));
    j["parameters"] = params;

    if (!fn.typeParameters.empty()) {
        json tps = json::array();
        for (const auto& tp : fn.typeParameters) tps.push_back(serializeTypeParameter(*tp));
        j["typeParameters"] = tps;
    }

    if (!fn.returnType.empty()) j["returnType"] = fn.returnType;

    json body = json::array();
    for (const auto& stmt : fn.body) body.push_back(serializeStatement(*stmt));
    j["body"] = body;

    return j;
}

json AstSerializer::serializeClassExpr(const ast::ClassExpression& cls) {
    json j;
    j["kind"] = "ClassExpression";
    addLocation(j, cls);
    if (!cls.name.empty()) j["name"] = cls.name;
    if (!cls.baseClass.empty()) j["baseClass"] = cls.baseClass;

    if (!cls.implementsInterfaces.empty()) {
        json ifaces = json::array();
        for (const auto& i : cls.implementsInterfaces) ifaces.push_back(i);
        j["implementsInterfaces"] = ifaces;
    }

    if (!cls.typeParameters.empty()) {
        json tps = json::array();
        for (const auto& tp : cls.typeParameters) tps.push_back(serializeTypeParameter(*tp));
        j["typeParameters"] = tps;
    }

    json members = json::array();
    for (const auto& m : cls.members) members.push_back(serializeNodePtr(m));
    j["members"] = members;

    return j;
}

json AstSerializer::serializeNewExpr(const ast::NewExpression& expr) {
    json j;
    j["kind"] = "NewExpression";
    addLocation(j, expr);
    j["expression"] = serializeExprPtr(expr.expression);

    json args = json::array();
    for (const auto& arg : expr.arguments) args.push_back(serializeExprPtr(arg));
    j["arguments"] = args;

    if (!expr.typeArguments.empty()) {
        json ta = json::array();
        for (const auto& t : expr.typeArguments) ta.push_back(t);
        j["typeArguments"] = ta;
    }

    return j;
}

json AstSerializer::serializeTemplateExpr(const ast::TemplateExpression& tmpl) {
    json j;
    j["kind"] = "TemplateExpression";
    addLocation(j, tmpl);
    j["head"] = tmpl.head;
    json spans = json::array();
    for (const auto& span : tmpl.spans) {
        json s;
        s["expression"] = serializeExprPtr(span.expression);
        s["literal"] = span.literal;
        spans.push_back(s);
    }
    j["templateSpans"] = spans;
    return j;
}

json AstSerializer::serializeTaggedTemplate(const ast::TaggedTemplateExpression& tt) {
    json j;
    j["kind"] = "TaggedTemplateExpression";
    addLocation(j, tt);
    j["tag"] = serializeExprPtr(tt.tag);
    j["template"] = serializeExprPtr(tt.templateExpr);
    return j;
}

json AstSerializer::serializeObjectLiteral(const ast::ObjectLiteralExpression& obj) {
    json j;
    j["kind"] = "ObjectLiteralExpression";
    addLocation(j, obj);
    json props = json::array();
    for (const auto& prop : obj.properties) props.push_back(serializeNodePtr(prop));
    j["properties"] = props;
    return j;
}

json AstSerializer::serializeArrayLiteral(const ast::ArrayLiteralExpression& arr) {
    json j;
    j["kind"] = "ArrayLiteralExpression";
    addLocation(j, arr);
    json elements = json::array();
    for (const auto& el : arr.elements) elements.push_back(serializeExprPtr(el));
    j["elements"] = elements;
    return j;
}

// ============================================================================
// Member/node serializers
// ============================================================================

json AstSerializer::serializeMethodDef(const ast::MethodDefinition& method) {
    json j;
    j["kind"] = "MethodDefinition";
    addLocation(j, method);
    j["name"] = method.name;
    if (method.nameNode) j["nameNode"] = serializeNodePtr(method.nameNode);
    if (method.isAsync) j["isAsync"] = true;
    if (method.isGenerator) j["isGenerator"] = true;

    json params = json::array();
    for (const auto& p : method.parameters) params.push_back(serializeParameter(*p));
    j["parameters"] = params;

    if (!method.typeParameters.empty()) {
        json tps = json::array();
        for (const auto& tp : method.typeParameters) tps.push_back(serializeTypeParameter(*tp));
        j["typeParameters"] = tps;
    }

    if (!method.returnType.empty()) j["returnType"] = method.returnType;

    j["access"] = accessModifierToString(method.access);
    if (method.isStatic) j["isStatic"] = true;
    if (method.isAbstract) j["isAbstract"] = true;
    if (method.isGetter) j["isGetter"] = true;
    if (method.isSetter) j["isSetter"] = true;
    if (!method.hasBody) j["hasBody"] = false;

    json body = json::array();
    for (const auto& stmt : method.body) body.push_back(serializeStatement(*stmt));
    j["body"] = body;

    if (!method.decorators.empty()) j["decorators"] = serializeDecorators(method.decorators);

    return j;
}

json AstSerializer::serializePropertyDef(const ast::PropertyDefinition& prop) {
    json j;
    j["kind"] = "PropertyDefinition";
    addLocation(j, prop);
    j["name"] = prop.name;
    j["type"] = prop.type;
    if (prop.initializer) j["initializer"] = serializeExprPtr(prop.initializer);
    j["access"] = accessModifierToString(prop.access);
    if (prop.isStatic) j["isStatic"] = true;
    if (prop.isReadonly) j["isReadonly"] = true;

    if (!prop.decorators.empty()) j["decorators"] = serializeDecorators(prop.decorators);

    return j;
}

json AstSerializer::serializeParameter(const ast::Parameter& param) {
    json j;
    j["name"] = serializeNodePtr(param.name);
    j["type"] = param.type;
    if (param.isOptional) j["isOptional"] = true;
    if (param.isRest) j["isRest"] = true;
    if (param.isThisParameter) j["isThisParameter"] = true;
    if (param.initializer) j["initializer"] = serializeNodePtr(param.initializer);
    if (param.access != ts::AccessModifier::Public) {
        j["access"] = accessModifierToString(param.access);
    }
    if (param.isReadonly) j["isReadonly"] = true;
    if (param.isParameterProperty) j["isParameterProperty"] = true;
    if (!param.decorators.empty()) j["decorators"] = serializeDecorators(param.decorators);
    addLocation(j, param);
    return j;
}

json AstSerializer::serializeTypeParameter(const ast::TypeParameter& tp) {
    json j;
    j["name"] = tp.name;
    if (!tp.constraint.empty()) j["constraint"] = tp.constraint;
    if (!tp.defaultType.empty()) j["default"] = tp.defaultType;
    addLocation(j, tp);
    return j;
}

} // namespace ts::parser
